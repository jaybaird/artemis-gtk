#include "pota_client.h"
#include "gio/gio.h"
#include "glib-object.h"
#include "glib.h"
#include "libsoup/soup-session.h"
#include "spot.h"

#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <unistd.h>

struct _PotaClient {
  GObject parent_instance;

  SoupSession *session;
  gchar       *auth_header;
  gchar       *source;
  gchar       *base_url;
};

typedef struct {
    SoupMessage *msg;
} TaskData;

static void task_data_free(TaskData *d) {
    if (!d) return;
    if (d->msg) g_object_unref(d->msg);
    g_free(d);
}

G_DEFINE_FINAL_TYPE(PotaClient, pota_client, G_TYPE_OBJECT)

JsonNode *build_json_from_spot(PotaClient *self, 
                               ArtemisSpot *spot, 
                               GError **error)
{
  const char *callsign          = artemis_spot_get_callsign(spot);
  const char *park_ref          = artemis_spot_get_park_ref(spot);
  const char *mode              = artemis_spot_get_mode(spot);
  const char *spotter_callsign  = artemis_spot_get_spotter(spot);
  const char *spotter_comment   = artemis_spot_get_spotter_comment(spot);
  int         freq_hz           = artemis_spot_get_frequency_hz(spot);
  g_autofree gchar *freq_str    = g_strdup_printf("%d", freq_hz);
  if (!callsign || !*callsign || !spotter_callsign || !*spotter_callsign || !park_ref || !*park_ref || freq_hz <= 0) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                  "Missing required fields (callsign/park_ref/frequency_hz)");
      return NULL;
  }

  JsonBuilder *b = json_builder_new();
  json_builder_begin_object(b);

  json_builder_set_member_name(b, "activator");
  json_builder_add_string_value(b, callsign);

  json_builder_set_member_name(b, "spotter");
  json_builder_add_string_value(b, spotter_callsign);

  json_builder_set_member_name(b, "frequency");
  json_builder_add_string_value(b, freq_str);

  json_builder_set_member_name(b, "reference");
  json_builder_add_string_value(b, park_ref);

  json_builder_set_member_name(b, "mode");
  json_builder_add_string_value(b, mode);

  json_builder_set_member_name(b, "source");
  json_builder_add_string_value(b, "Web");

  if (spotter_comment) {
    json_builder_set_member_name(b, "comments");
    json_builder_add_string_value(b, spotter_comment);
  }

  json_builder_end_object(b);

  JsonNode *root = json_builder_get_root(b);
  g_object_unref(b);

  return root;
}

static void post_spot_cb(GObject *source, GAsyncResult *res, gpointer user_data) {
  GTask       *task = G_TASK(user_data);
  PotaClient  *self = g_task_get_source_object(task);
  TaskData *td  = g_task_get_task_data(task);
  (void)self;

  GError *error = NULL;
  GBytes *body  = soup_session_send_and_read_finish(SOUP_SESSION(source), res, &error);
  if (error) 
  {
    g_task_return_error(task, error);
    g_object_unref(task);
    return;
  }

  guint status = soup_message_get_status(td->msg);
  if (status < 200 || status >= 300) 
  {
    const char *phrase = soup_status_get_phrase(status);
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "HTTP %u %s", status, phrase ? phrase : "");
    if (body) g_bytes_unref(body);
    g_object_unref(task);
    return;
  }

  JsonNode *result_root = NULL;
  if (body) 
  {
    gsize len = 0;
    const char *data = g_bytes_get_data(body, &len);
    if (len > 0 && data) {
      JsonParser *parser = json_parser_new();
      if (json_parser_load_from_data(parser, data, len, NULL))
        result_root = json_parser_steal_root(parser);
      g_object_unref(parser);
    }
    g_bytes_unref(body);
  }

  g_task_return_pointer(task, result_root, (GDestroyNotify)json_node_unref);
  g_object_unref(task);
}

void pota_client_post_spot_async(PotaClient *self,
                                 ArtemisSpot *spot,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
  g_return_if_fail(ARTEMIS_IS_POTA_CLIENT(self));
  g_return_if_fail(ARTEMIS_IS_SPOT(spot));

  GError *err = NULL;
  JsonNode *root = build_json_from_spot(self, spot, &err);
  if (!root)
  {
    g_task_report_error(self, callback, user_data, NULL, err);
    return;
  }

  JsonGenerator *gen = json_generator_new();
  json_generator_set_root(gen, root);
  gsize payload_len = 0;
  gchar *payload = json_generator_to_data(gen, &payload_len);
  json_node_unref(root);
  g_object_unref(gen);

  GBytes *bytes = g_bytes_new_take(payload, payload_len);
  g_autofree gchar *url = g_strconcat(self->base_url, "/spot", NULL);

  SoupMessage *msg = soup_message_new("POST", url);

  soup_message_set_request_body_from_bytes(msg, "application/json", bytes);
  g_bytes_unref(bytes);

  SoupMessageHeaders *hdr = soup_message_get_request_headers(msg);
  soup_message_headers_replace(hdr, "Accept", "application/json");
  soup_message_headers_replace(hdr, "User-Agent", "Artemis/1.0 (+POTA client)");
  if (self->auth_header && *self->auth_header)
  {    
    soup_message_headers_replace(hdr, "Authorization", self->auth_header);
  }

  GTask *task = g_task_new(self, cancellable, callback, user_data);
  TaskData *data = g_new0(TaskData, 1);
  data->msg = g_object_ref(msg);
  g_task_set_task_data(task, data, (GDestroyNotify)task_data_free);

  soup_session_send_and_read_async(self->session, msg, G_PRIORITY_DEFAULT, cancellable, post_spot_cb, task);
  g_object_unref(msg);
}

JsonNode *pota_client_post_spot_finish(PotaClient *self, GAsyncResult *res, GError **error) {
  g_return_val_if_fail(ARTEMIS_IS_POTA_CLIENT(self), NULL);
  g_return_val_if_fail(g_task_is_valid(res, self), NULL);
  return g_task_propagate_pointer(G_TASK(res), error);
}

static void get_spots_cb(GObject *source, GAsyncResult *res, gpointer user_data) {
  GTask *task = G_TASK(user_data);
  PotaClient *self = g_task_get_source_object(task);
  TaskData *td = g_task_get_task_data(task);
  (void)self;

  GError *error = NULL;
  GBytes *body  = soup_session_send_and_read_finish(SOUP_SESSION(source), res, &error);
  if (error) {
    g_task_return_error(task, error);
    g_object_unref(task);
    return;
  }

  guint status = soup_message_get_status(td->msg);
  if (status < 200 || status >= 300) {
    const char *phrase = soup_status_get_phrase(status);
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "HTTP %u %s", status, phrase ? phrase : "");
    if (body) g_bytes_unref(body);
    g_object_unref(task);
    return;
  }

  JsonNode *root = NULL;
  if (body) {
    gsize len = 0;
    const char *data = g_bytes_get_data(body, &len);
    if (len > 0 && data) {
      JsonParser *parser = json_parser_new();
      if (json_parser_load_from_data(parser, data, len, NULL))
        root = json_parser_steal_root(parser);
      g_object_unref(parser);
    }
    g_bytes_unref(body);
  }

  g_task_return_pointer(task, root, (GDestroyNotify)json_node_unref);
  g_object_unref(task);
}

void pota_client_get_spots_async(PotaClient *self,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
  g_return_if_fail(ARTEMIS_IS_POTA_CLIENT(self));

  g_autofree gchar *url = g_strconcat(self->base_url, "/v1/spots", NULL);
  SoupMessage *msg = soup_message_new("GET", url);

  SoupMessageHeaders *hdr = soup_message_get_request_headers(msg);
  soup_message_headers_replace(hdr, "Accept", "application/json");
  soup_message_headers_replace(hdr, "User-Agent", "Artemis/1.0 (+POTA client)");
  if (self->auth_header && *self->auth_header)
  {
    soup_message_headers_replace(hdr, "Authorization", self->auth_header);
  }

  GTask *task = g_task_new(self, cancellable, callback, user_data);
  TaskData *td = g_new0(TaskData, 1);
  td->msg = g_object_ref(msg);
  g_task_set_task_data(task, td, (GDestroyNotify)task_data_free);

  soup_session_send_and_read_async(self->session, msg, G_PRIORITY_DEFAULT,
                                    cancellable, get_spots_cb, task);

  g_object_unref(msg);
}

JsonNode *pota_client_get_spots_finish(PotaClient *self, GAsyncResult *res, GError **error) {
  g_return_val_if_fail(ARTEMIS_IS_POTA_CLIENT(self), NULL);
  g_return_val_if_fail(g_task_is_valid(res, self), NULL);
  return g_task_propagate_pointer(G_TASK(res), error);
}

static void get_activator_cb(GObject *source, GAsyncResult *res, gpointer user_data) {
  GTask *task = G_TASK(user_data);
  PotaClient *self = g_task_get_source_object(task);
  TaskData *td = g_task_get_task_data(task);
  (void)self;

  GError *error = NULL;
  GBytes *body  = soup_session_send_and_read_finish(SOUP_SESSION(source), res, &error);
  if (error) {
    g_task_return_error(task, error);
    g_object_unref(task);
    return;
  }

  guint status = soup_message_get_status(td->msg);
  if (status < 200 || status >= 300) {
    const char *phrase = soup_status_get_phrase(status);
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                            "HTTP %u %s", status, phrase ? phrase : "");
    if (body) g_bytes_unref(body);
    g_object_unref(task);
    return;
  }

  JsonNode *root = NULL;
  if (body) {
    gsize len = 0;
    const char *data = g_bytes_get_data(body, &len);
    if (len > 0 && data) {
      JsonParser *parser = json_parser_new();
      if (json_parser_load_from_data(parser, data, len, NULL))
        root = json_parser_steal_root(parser);
      g_object_unref(parser);
    }
    g_bytes_unref(body);
  }

  g_task_return_pointer(task, root, (GDestroyNotify)json_node_unref);
  g_object_unref(task);
}

void pota_client_get_activator_async(PotaClient *self,
                                     const gchar *callsign,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
  g_return_if_fail(ARTEMIS_IS_POTA_CLIENT(self));
  g_return_if_fail(callsign && *callsign);

  g_autofree gchar *escaped_callsign = g_uri_escape_string(callsign, NULL, FALSE);
  g_autofree gchar *url = g_strconcat(self->base_url, "/stats/user/", escaped_callsign, NULL);
  SoupMessage *msg = soup_message_new("GET", url);

  SoupMessageHeaders *hdr = soup_message_get_request_headers(msg);
  soup_message_headers_replace(hdr, "Accept", "application/json");
  soup_message_headers_replace(hdr, "User-Agent", "Artemis/1.0 (+POTA client)");
  if (self->auth_header && *self->auth_header)
  {
    soup_message_headers_replace(hdr, "Authorization", self->auth_header);
  }

  GTask *task = g_task_new(self, cancellable, callback, user_data);
  TaskData *td = g_new0(TaskData, 1);
  td->msg = g_object_ref(msg);
  g_task_set_task_data(task, td, (GDestroyNotify)task_data_free);

  soup_session_send_and_read_async(self->session, msg, G_PRIORITY_DEFAULT,
                                    cancellable, get_activator_cb, task);

  g_object_unref(msg);
}

JsonNode *pota_client_get_activator_finish(PotaClient *self, GAsyncResult *res, GError **error) {
  g_return_val_if_fail(ARTEMIS_IS_POTA_CLIENT(self), NULL);
  g_return_val_if_fail(g_task_is_valid(res, self), NULL);
  return g_task_propagate_pointer(G_TASK(res), error);
}

static void pota_client_finalize(GObject *obj)
{
  PotaClient *self = ARTEMIS_POTA_CLIENT(obj);
  
  g_clear_object(&self->session);
  g_clear_pointer(&self->auth_header, g_free);
  g_clear_pointer(&self->source, g_free);
  g_clear_pointer(&self->base_url, g_free);

  G_OBJECT_CLASS(pota_client_parent_class)->finalize(obj);
}

static void pota_client_class_init(PotaClientClass *klass)
{
  GObjectClass *gclass = G_OBJECT_CLASS(klass);
  gclass->finalize = pota_client_finalize;
}

static void pota_client_init(PotaClient *self)
{
  self->session = soup_session_new();
  self->source = g_strdup("Artemis/1.0");
  self->base_url = g_strdup("https://api.pota.app");
  g_object_set(self->session, "timeout", 30, "idle-timeout", 15, NULL);
}

PotaClient *pota_client_new()
{
  return g_object_new(POTA_TYPE_CLIENT, NULL);
}

void pota_client_set_timeout(PotaClient *self, guint io_timeout_sec, guint idle_timeout_sec)
{
  g_return_if_fail(ARTEMIS_IS_POTA_CLIENT(self));

  g_object_set(self->session, 
    "timeout", io_timeout_sec,
  "idle-timeout", idle_timeout_sec, 
  NULL);
}