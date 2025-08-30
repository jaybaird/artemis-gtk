#include "pota_user_cache.h"
#include <json-glib/json-glib.h>

typedef struct _PotaUserCacheEntry {
  ArtemisActivator *activator;
  gint64            expires_at; // monotonic time when entry expires
} PotaUserCacheEntry;

struct _ArtemisPotaUserCache {
  GObject parent_instance;
  
  PotaClient *client;
  GHashTable *cache; // callsign -> PotaUserCacheEntry
  guint       default_ttl_seconds;
};

// Singleton instance
static ArtemisPotaUserCache *g_pota_user_cache_instance = NULL;
static GMutex g_pota_user_cache_mutex;

G_DEFINE_FINAL_TYPE(ArtemisPotaUserCache, artemis_pota_user_cache, G_TYPE_OBJECT)

static void pota_user_cache_entry_free(PotaUserCacheEntry *entry) {
  if (entry) {
    g_clear_object(&entry->activator);
    g_free(entry);
  }
}

static void artemis_pota_user_cache_finalize(GObject *object) {
  ArtemisPotaUserCache *self = ARTEMIS_POTA_USER_CACHE(object);
  
  g_clear_pointer(&self->cache, g_hash_table_unref);
  g_clear_object(&self->client);
  
  G_OBJECT_CLASS(artemis_pota_user_cache_parent_class)->finalize(object);
}

static void artemis_pota_user_cache_class_init(ArtemisPotaUserCacheClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = artemis_pota_user_cache_finalize;
}

static void artemis_pota_user_cache_init(ArtemisPotaUserCache *self) {
  self->cache = g_hash_table_new_full(g_str_hash, g_str_equal, 
                                      g_free, (GDestroyNotify)pota_user_cache_entry_free);
  self->default_ttl_seconds = 3600; // 1 hour default
}

ArtemisPotaUserCache *artemis_pota_user_cache_new(PotaClient *client) {
  g_return_val_if_fail(ARTEMIS_IS_POTA_CLIENT(client), NULL);
  
  ArtemisPotaUserCache *self = g_object_new(ARTEMIS_TYPE_POTA_USER_CACHE, NULL);
  self->client = g_object_ref(client);
  return self;
}

static gboolean is_entry_expired(PotaUserCacheEntry *entry) {
  if (!entry) return TRUE;
  return g_get_monotonic_time() > entry->expires_at;
}

static void get_activator_from_api_cb(GObject *source, GAsyncResult *res, gpointer user_data) {
  GTask *task = G_TASK(user_data);
  ArtemisPotaUserCache *self = g_task_get_source_object(task);
  PotaClient *client = ARTEMIS_POTA_CLIENT(source);
  
  GError *error = NULL;
  JsonNode *root = pota_client_get_activator_finish(client, res, &error);
  
  if (error) {
    g_task_return_error(task, error);
    g_object_unref(task);
    return;
  }
  
  ArtemisActivator *activator = NULL;
  if (root && JSON_NODE_HOLDS_OBJECT(root)) {
    JsonObject *obj = json_node_get_object(root);
    activator = artemis_activator_new_from_json(obj);
    
    if (activator) {
      // Cache the result
      const gchar *callsign = artemis_activator_get_callsign(activator);
      guint ttl_seconds = GPOINTER_TO_UINT(g_task_get_task_data(task));
      
      PotaUserCacheEntry *entry = g_new0(PotaUserCacheEntry, 1);
      entry->activator = g_object_ref(activator);
      entry->expires_at = g_get_monotonic_time() + (ttl_seconds * G_TIME_SPAN_SECOND);
      
      g_hash_table_replace(self->cache, g_strdup(callsign), entry);
    }
  }
  
  if (root) json_node_unref(root);
  
  g_task_return_pointer(task, activator, g_object_unref);
  g_object_unref(task);
}

void artemis_pota_user_cache_get_async(ArtemisPotaUserCache *self,
                                       const gchar          *callsign,
                                       guint                 ttl_seconds,
                                       GCancellable         *cancellable,
                                       GAsyncReadyCallback   callback,
                                       gpointer              user_data) {
  g_return_if_fail(ARTEMIS_IS_POTA_USER_CACHE(self));
  g_return_if_fail(callsign && *callsign);
  
  if (ttl_seconds == 0) {
    ttl_seconds = self->default_ttl_seconds;
  }
  
  // Check cache first
  PotaUserCacheEntry *entry = g_hash_table_lookup(self->cache, callsign);
  if (entry && !is_entry_expired(entry)) {
    // Cache hit - return immediately
    GTask *task = g_task_new(self, cancellable, callback, user_data);
    g_task_return_pointer(task, g_object_ref(entry->activator), g_object_unref);
    g_object_unref(task);
    return;
  }
  
  // Cache miss or expired - fetch from API
  GTask *task = g_task_new(self, cancellable, callback, user_data);
  g_task_set_task_data(task, GUINT_TO_POINTER(ttl_seconds), NULL);
  
  pota_client_get_activator_async(self->client, callsign, cancellable, 
                                  get_activator_from_api_cb, task);
}

ArtemisActivator *artemis_pota_user_cache_get_finish(ArtemisPotaUserCache *self,
                                                     GAsyncResult         *result,
                                                     GError              **error) {
  g_return_val_if_fail(ARTEMIS_IS_POTA_USER_CACHE(self), NULL);
  g_return_val_if_fail(g_task_is_valid(result, self), NULL);
  
  return g_task_propagate_pointer(G_TASK(result), error);
}

void artemis_pota_user_cache_clear(ArtemisPotaUserCache *self) {
  g_return_if_fail(ARTEMIS_IS_POTA_USER_CACHE(self));
  g_hash_table_remove_all(self->cache);
}

void artemis_pota_user_cache_set_ttl_default(ArtemisPotaUserCache *self, guint ttl_seconds) {
  g_return_if_fail(ARTEMIS_IS_POTA_USER_CACHE(self));
  self->default_ttl_seconds = ttl_seconds;
}

/* ----------------- Singleton implementation ----------------- */
ArtemisPotaUserCache *artemis_pota_user_cache_get_instance(void)
{
  g_mutex_lock(&g_pota_user_cache_mutex);
  
  if (!g_pota_user_cache_instance) {
    // Create with default client - will be set by spot repo
    PotaClient *client = pota_client_new();
    g_pota_user_cache_instance = artemis_pota_user_cache_new(client);
    g_object_unref(client); // cache holds its own reference
  }
  
  g_mutex_unlock(&g_pota_user_cache_mutex);
  return g_pota_user_cache_instance;
}

void artemis_pota_user_cache_cleanup_instance(void)
{
  g_mutex_lock(&g_pota_user_cache_mutex);
  
  if (g_pota_user_cache_instance) {
    g_object_unref(g_pota_user_cache_instance);
    g_pota_user_cache_instance = NULL;
  }
  
  g_mutex_unlock(&g_pota_user_cache_mutex);
}