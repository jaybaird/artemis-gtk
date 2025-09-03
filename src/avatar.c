#include "avatar.h"

void avatar_update_data_free(AvatarUpdateData *data) {
  if (data) {
    g_free(data->callsign);
    g_free(data);
  }
}

// Generate Gravatar URL from hash
static gchar* generate_gravatar_url(const char *gravatar_hash) {
  if (!gravatar_hash || !*gravatar_hash) return NULL;
  
  return g_strdup_printf("https://www.gravatar.com/avatar/%s?s=64&d=identicon", gravatar_hash);
}

static void on_gravatar_loaded(GObject *source, GAsyncResult *result, gpointer user_data) {
  SoupSession *session = SOUP_SESSION(source);
  AvatarUpdateData *data = (AvatarUpdateData *)user_data;
  
  GError *error = NULL;
  GBytes *bytes = soup_session_send_and_read_finish(session, result, &error);
  
  if (bytes && !error) {
    g_debug("Loaded Gravatar bytes: %zu bytes", g_bytes_get_size(bytes));
    
    GdkTexture *texture = gdk_texture_new_from_bytes(bytes, &error);
    if (texture && !error) {
      g_debug("Successfully created texture for Gravatar");
      adw_avatar_set_custom_image(data->target_avatar, GDK_PAINTABLE(texture));
      g_object_unref(texture);
    } else {
      g_debug("Failed to create texture from Gravatar bytes: %s", error ? error->message : "unknown");
      g_clear_error(&error);
    }
    g_bytes_unref(bytes);
  } else {
    g_debug("Failed to load Gravatar: %s", error ? error->message : "unknown error");
    g_clear_error(&error);
  }
  
  // Only fallback to text if Gravatar failed - don't override successful Gravatar
  // The text was already set before we started loading Gravatar
  
  avatar_update_data_free(data);
}

// Global session for Gravatar requests
static SoupSession *gravatar_session = NULL;

static SoupSession *get_gravatar_session(void) {
  if (!gravatar_session) {
    gravatar_session = soup_session_new();
  }
  return gravatar_session;
}

void avatar_fetch_gravatar_async(const char *gravatar_hash, AvatarUpdateData *data) {
  g_autofree gchar *gravatar_url = generate_gravatar_url(gravatar_hash);
  if (!gravatar_url) {
    g_debug("No gravatar_url generated for hash: %s", gravatar_hash ? gravatar_hash : "NULL");
    avatar_update_data_free(data);
    return;
  }
  
  g_debug("Fetching Gravatar from: %s for callsign: %s", gravatar_url, data->callsign ? data->callsign : "NULL");
  
  SoupSession *session = get_gravatar_session();
  SoupMessage *msg = soup_message_new("GET", gravatar_url);
  
  soup_session_send_and_read_async(session, msg, G_PRIORITY_DEFAULT, NULL,
                                   on_gravatar_loaded, data);
  
  g_object_unref(msg);
}
