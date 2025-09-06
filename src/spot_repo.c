#include "spot_repo.h"
#include "gio/gio.h"

#include "glib-object.h"
#include "glib.h"
#include "gobject/gmarshal.h"
#include "pota_client.h"
#include "pota_user_cache.h"
#include "spot.h"
#include "database.h"

enum {
  SIGNAL_BUSY_CHANGED,
  SIGNAL_REFRESHED,
  SIGNAL_ERROR,
  N_SIGNALS
};
static guint signals[N_SIGNALS];

struct _ArtemisSpotRepo {
  GObject parent_instance;

  GListStore *spot_store;
  GListStore *ham_store;

  PotaClient *client;
  ArtemisPotaUserCache *pota_user_cache;

  gboolean busy;
};

G_DEFINE_FINAL_TYPE(ArtemisSpotRepo, artemis_spot_repo, G_TYPE_OBJECT)

static void repo_set_busy(ArtemisSpotRepo *self, gboolean busy) 
{
  if (self->busy == busy) return;
  self->busy = busy;
  g_signal_emit(self, signals[SIGNAL_BUSY_CHANGED], 0, busy);
}

static void artemis_spot_repo_dispose(GObject *obj) 
{
  ArtemisSpotRepo *self = ARTEMIS_SPOT_REPO(obj);
  g_clear_object(&self->spot_store);
  g_clear_object(&self->ham_store);
  g_clear_object(&self->client);
  g_clear_object(&self->pota_user_cache);

  G_OBJECT_CLASS(artemis_spot_repo_parent_class)->dispose(obj);
}

static void artemis_spot_repo_class_init(ArtemisSpotRepoClass *klass) 
{
  GObjectClass *oc = G_OBJECT_CLASS(klass);
  oc->dispose = artemis_spot_repo_dispose;

  signals[SIGNAL_BUSY_CHANGED] = g_signal_new(
    "busy-changed",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
    G_TYPE_NONE, 1, G_TYPE_BOOLEAN
  );

  signals[SIGNAL_REFRESHED] = g_signal_new(
    "refreshed",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL, g_cclosure_marshal_VOID__UINT,
    G_TYPE_NONE, 1, G_TYPE_UINT
  );

  signals[SIGNAL_ERROR] = g_signal_new(
    "error",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0, NULL, NULL, g_cclosure_marshal_VOID__BOXED,
    G_TYPE_NONE, 1, G_TYPE_ERROR
  );
}

static void artemis_spot_repo_init(ArtemisSpotRepo *self) 
{
  self->spot_store = g_list_store_new(ARTEMIS_TYPE_SPOT);
  self->client = pota_client_new();
  self->pota_user_cache = artemis_pota_user_cache_new(self->client);
}

typedef struct {
  ArtemisSpotRepo *repo;
  guint ttl_seconds;
  guint n_spots_added;
  GHashTable *unique_callsigns; // tracks unique callsigns to avoid duplicate requests
} SpotUpdateData;

static void
spot_update_data_free(SpotUpdateData *data) {
  if (data) {
    if (data->unique_callsigns) g_hash_table_unref(data->unique_callsigns);
    g_object_unref(data->repo);
    g_free(data);
  }
}

static void
on_user_data_fetched(GObject *source, GAsyncResult *result, gpointer user_data) {
  // This callback is fired for each user data fetch (activators and spotters), but we don't need to do much here
  // The user cache handles storing the results
  ArtemisPotaUserCache *cache = ARTEMIS_POTA_USER_CACHE(source);
  GError *error = NULL;
  ArtemisActivator *activator = artemis_pota_user_cache_get_finish(cache, result, &error);
  
  // Log or ignore errors - the cache will handle them
  if (error) {
    g_debug("Failed to fetch user data: %s", error->message);
    g_error_free(error);
  }
  if (activator) {
    g_object_unref(activator);
  }
}

static void on_update_spots(GObject *src, GAsyncResult *result, gpointer user_data)
{
  PotaClient *client = ARTEMIS_POTA_CLIENT(src);
  SpotUpdateData *data = (SpotUpdateData *)user_data;
  ArtemisSpotRepo *self = data->repo;

  g_list_store_remove_all(self->spot_store);

  GError *err = NULL;
  JsonNode *root = pota_client_get_spots_finish(client, result, &err);

  if (err)
  {
    g_list_store_remove_all(self->spot_store); // remove all on error
    g_signal_emit(self, signals[SIGNAL_ERROR], 0, err);
    repo_set_busy(self, FALSE);
    spot_update_data_free(data);
    return;
  }

  guint n_added = 0;
  if (JSON_NODE_HOLDS_ARRAY(root)) {
    JsonArray *arr = json_node_get_array(root);
    // clear + repopulate
    guint n = g_list_model_get_n_items(G_LIST_MODEL(self->spot_store));
    for (gint i = (gint)n - 1; i >= 0; --i) g_list_store_remove(self->spot_store, i);

    guint len = json_array_get_length(arr);
    for (guint i = 0; i < len; ++i) {
      JsonObject *o = json_array_get_object_element(arr, i);
      if (!o) continue;
      ArtemisSpot *spot = artemis_spot_new_from_json(o);
      g_list_store_append(self->spot_store, spot);
      
      // Check if this spot was posted by the user from an external program
      const char *spotter = artemis_spot_get_spotter(spot);
      if (spotter && *spotter) {
        GSettings *settings = g_settings_new("com.k0vcz.artemis");
        g_autofree gchar *user_callsign = g_settings_get_string(settings, "callsign");
        
        if (user_callsign && g_strcmp0(spotter, user_callsign) == 0) {
          // This is a spot posted by the user from an external program
          // Add it to the database as a hunted QSO
          const char *callsign = artemis_spot_get_callsign(spot);
          const char *park_ref = artemis_spot_get_park_ref(spot);
          
          if (callsign && park_ref) {
            g_debug("Auto-marking externally spotted park as hunted: %s @ %s", callsign, park_ref);
            sqlite3_int64 qso_id = 0;
            GError *db_err = NULL;
            if (!spot_db_add_qso_from_spot(spot_db_get_instance(), spot, &qso_id, &db_err)) {
              g_warning("Failed to add externally spotted QSO to database: %s", 
                       db_err ? db_err->message : "Unknown error");
              g_clear_error(&db_err);
            }
          }
        }
        
        g_object_unref(settings);
      }
      
      // Fetch activator data for unique callsigns
      const char *callsign = artemis_spot_get_callsign(spot);
      if (callsign && *callsign && !g_hash_table_contains(data->unique_callsigns, callsign)) {
        g_hash_table_add(data->unique_callsigns, g_strdup(callsign));
        artemis_pota_user_cache_get_async(self->pota_user_cache, callsign, data->ttl_seconds,
                                          NULL, on_user_data_fetched, NULL);
      }
      
      // Also fetch spotter/hunter data for unique callsigns
      if (spotter && *spotter && !g_hash_table_contains(data->unique_callsigns, spotter)) {
        g_hash_table_add(data->unique_callsigns, g_strdup(spotter));
        artemis_pota_user_cache_get_async(self->pota_user_cache, spotter, data->ttl_seconds,
                                          NULL, on_user_data_fetched, NULL);
      }
      
      g_object_unref(spot);
      n_added++;
    }
  }

  if (root) json_node_unref(root);
  
  data->n_spots_added = n_added;
  repo_set_busy(self, FALSE);
  g_signal_emit(self, signals[SIGNAL_REFRESHED], 0, n_added);
  spot_update_data_free(data);
}

gboolean artemis_spot_repo_get_busy(ArtemisSpotRepo *self) 
{
  g_return_val_if_fail(ARTEMIS_IS_SPOT_REPO(self), FALSE);
  return self->busy;
}

ArtemisSpotRepo *artemis_spot_repo_new(void) 
{
  return g_object_new(ARTEMIS_TYPE_SPOT_REPO, NULL);
}

GListModel *artemis_spot_repo_get_model(ArtemisSpotRepo *self) 
{
  g_return_val_if_fail(ARTEMIS_IS_SPOT_REPO(self), NULL);
  return G_LIST_MODEL(self->spot_store); // borrowed
}

void artemis_spot_repo_update_spots(ArtemisSpotRepo *self, guint ttl_secs) 
{
  g_return_if_fail(ARTEMIS_IS_SPOT_REPO(self));
  repo_set_busy(self, TRUE);

  SpotUpdateData *data = g_new0(SpotUpdateData, 1);
  data->repo = g_object_ref(self);
  data->ttl_seconds = ttl_secs > 0 ? ttl_secs : 3600; // Default 1 hour TTL
  data->unique_callsigns = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  pota_client_get_spots_async(self->client, NULL, on_update_spots, data);
}

PotaClient *artemis_spot_repo_get_pota_client(ArtemisSpotRepo *self)
{
  return self->client;
}

ArtemisPotaUserCache *artemis_spot_repo_get_pota_user_cache(ArtemisSpotRepo *self)
{
  g_return_val_if_fail(ARTEMIS_IS_SPOT_REPO(self), NULL);
  return self->pota_user_cache;
}

