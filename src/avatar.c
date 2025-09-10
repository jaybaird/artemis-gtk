#include "avatar.h"

#include "dex_soup.h"
#include "glib-object.h"
#include "glib.h"
#include "utils.h"

void
avatar_update_data_free(AvatarUpdateData *data) {
  if (data) {
    g_free(data->callsign);
    g_free(data);
  }
}

// Generate Gravatar URL from hash
static gchar*
generate_gravatar_url(const char *gravatar_hash) {
  if (!gravatar_hash || !*gravatar_hash) return NULL;
  
  return g_strdup_printf("https://www.gravatar.com/avatar/%s?s=64&d=identicon", gravatar_hash);
}

// Global session for Gravatar requests
static SoupSession *gravatar_session = NULL;
static SoupCache *gravatar_cache = NULL;

static SoupSession *get_gravatar_session(void) {
  if (!gravatar_session) {
    gravatar_session = soup_session_new_with_options("timeout", 15, NULL);

    const gchar *data_dir = g_get_user_data_dir();
    g_autofree gchar *app_dir = g_build_filename(data_dir, "artemis", NULL);
    g_mkdir_with_parents(app_dir, 0700);
    g_autofree gchar *cache_path = g_build_filename(app_dir, "gravatar.cache", NULL);
    gravatar_cache = soup_cache_new(cache_path, SOUP_CACHE_SINGLE_USER);

    soup_session_add_feature(gravatar_session, SOUP_SESSION_FEATURE(gravatar_cache));
  }
  return gravatar_session;
}

typedef struct {
  AdwAvatar   *avatar;
  gchar       *gravatar_hash;
  gchar       *callsign;
} _WorkerData;

static void 
worker_data_free(_WorkerData *data)
{
  if (data) {
    g_debug("worker_data_free called: data=%p, avatar=%p, hash=%p, callsign=%p", 
            data, data->avatar, data->gravatar_hash, data->callsign);
    if (data->callsign) {
      g_debug("  callsign content: %s", data->callsign);
    }
    if (data->avatar) {
      g_object_unref(data->avatar);
    }
    g_free(data->gravatar_hash);
    g_free(data->callsign);
    g_free(data);
    g_debug("worker_data_free completed");
  }
}

typedef struct {
  AdwAvatar   *avatar;
  GdkTexture  *texture;
} _AvatarUpdateData;

static void update_data_free(_AvatarUpdateData *data)
{
  if (data)
  {
    g_object_unref(data->avatar);
    g_object_unref(data->texture);
    g_free(data);
  }
}

static DexFuture *
update_avatar_worker(gpointer user_data)
{
  _AvatarUpdateData *data = (_AvatarUpdateData *)user_data;
  adw_avatar_set_custom_image(data->avatar, GDK_PAINTABLE(data->texture));
  return dex_future_new_true();
}

static DexFuture *
fetch_gravatar_worker(gpointer user_data)
{
  _WorkerData *data = (_WorkerData *)user_data;

  SoupSession *session = get_gravatar_session();
  g_autofree gchar *gravatar_url = generate_gravatar_url(data->gravatar_hash);
  g_autoptr (SoupMessage) message = soup_message_new("GET", gravatar_url);
  g_autoptr (GError) error = NULL;
  g_autoptr (GInputStream) input = NULL;
  g_autoptr (GBytes) bytes = NULL;

  if (!gravatar_url) {
    return dex_future_new_reject (G_URI_ERROR,
                                  G_URI_ERROR_FAILED,
                                  "Failed to create url \"%s\"", data->gravatar_hash);
  }
  
  g_debug("Fetching Gravatar from: %s for callsign: %s", gravatar_url, data->callsign ? data->callsign : "NULL");

  input = dex_await_object(artemis_session_send(session, message, G_PRIORITY_DEFAULT), &error);
  return_if_error(error);

  bytes = dex_await_boxed(dex_input_stream_read_bytes(input, 10 * 1024 * 1024, G_PRIORITY_DEFAULT), &error);
  return_if_error(error);

  g_debug("Loaded Gravatar bytes: %zu bytes", g_bytes_get_size(bytes));
    
  g_autoptr (GdkTexture) texture = gdk_texture_new_from_bytes(bytes, &error);
  return_if_error(error);

  if (texture) {
    _AvatarUpdateData *main_thread_data = g_new0(_AvatarUpdateData, 1);
    main_thread_data->avatar = g_object_ref(data->avatar);
    main_thread_data->texture = g_object_ref(texture);
    return dex_scheduler_spawn(
      dex_scheduler_get_default(), 
      0, 
      update_avatar_worker, 
      main_thread_data, 
      (GDestroyNotify)update_data_free
    );
  }

  return dex_future_new_true();
}

DexFuture *
avatar_fetch_gravatar_async(gchar *gravatar_hash, AdwAvatar *avatar, gchar *callsign) {
  _WorkerData *worker_data = g_new0(_WorkerData, 1);
  worker_data->avatar = g_object_ref(avatar);
  worker_data->gravatar_hash = g_strdup(gravatar_hash);
  worker_data->callsign = g_strdup(callsign);
  
  g_debug("avatar_fetch_gravatar_async: allocated data=%p for callsign=%s", 
          worker_data, callsign ? callsign : "NULL");
  
  return dex_scheduler_spawn(
    dex_thread_pool_scheduler_get_default(), 
    0, 
    fetch_gravatar_worker, 
    worker_data, 
    (GDestroyNotify)worker_data_free
  );
}
