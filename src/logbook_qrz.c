// logbook_qrz.c - QRZ logbook provider implementation
#include "logbook_qrz.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

struct _LogbookQrz {
    LogbookProvider parent_instance;
    SoupSession *session;
    gchar *api_key;
};

G_DEFINE_FINAL_TYPE(LogbookQrz, logbook_qrz, LOGBOOK_TYPE_PROVIDER)

static void logbook_qrz_finalize(GObject *object);
static gboolean logbook_qrz_is_configured(LogbookProvider *provider);
static void logbook_qrz_log_qso_async(LogbookProvider *provider,
                                      LogbookQso *qso,
                                      GCancellable *cancellable,
                                      GAsyncReadyCallback callback,
                                      gpointer user_data);
static gboolean logbook_qrz_log_qso_finish(LogbookProvider *provider,
                                           GAsyncResult *result,
                                           GError **error);
static const gchar* logbook_qrz_get_name(LogbookProvider *provider);

static void
logbook_qrz_class_init(LogbookQrzClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    LogbookProviderClass *provider_class = LOGBOOK_PROVIDER_CLASS(klass);
    
    object_class->finalize = logbook_qrz_finalize;
    
    provider_class->is_configured = logbook_qrz_is_configured;
    provider_class->log_qso_async = logbook_qrz_log_qso_async;
    provider_class->log_qso_finish = logbook_qrz_log_qso_finish;
    provider_class->get_name = logbook_qrz_get_name;
}

static void
logbook_qrz_init(LogbookQrz *self)
{
    self->session = soup_session_new_with_options(
        "timeout", 30,
        "idle-timeout", 15,
        NULL
    );
}

static void
logbook_qrz_finalize(GObject *object)
{
    LogbookQrz *self = LOGBOOK_QRZ(object);
    
    if (self->session) {
        g_object_unref(self->session);
    }
    g_free(self->api_key);
    
    G_OBJECT_CLASS(logbook_qrz_parent_class)->finalize(object);
}

LogbookQrz*
logbook_qrz_new(void)
{
    return g_object_new(LOGBOOK_TYPE_QRZ, NULL);
}

static gboolean
logbook_qrz_is_configured(LogbookProvider *provider)
{
    LogbookQrz *self = LOGBOOK_QRZ(provider);
    
    // Load API key from settings if not cached
    if (!self->api_key) {
        g_autoptr(GSettings) settings = g_settings_new("com.k0vcz.artemis");
        self->api_key = g_settings_get_string(settings, "qrz-api-key");
    }
    
    return self->api_key && strlen(self->api_key) > 0;
}

static const gchar*
logbook_qrz_get_name(LogbookProvider *provider)
{
    return "QRZ Logbook";
}

typedef struct {
    LogbookQrz *qrz;
    LogbookQso *qso;
    GTask *task;
} QrzLogTask;

static void
qrz_log_task_free(QrzLogTask *log_task)
{
    if (log_task->qrz) {
        g_object_unref(log_task->qrz);
    }
    if (log_task->qso) {
        logbook_qso_free(log_task->qso);
    }
    if (log_task->task) {
        g_object_unref(log_task->task);
    }
    g_free(log_task);
}

static void
qrz_log_response_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    QrzLogTask *log_task = (QrzLogTask*)user_data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GBytes) response_body = NULL;
    
    response_body = soup_session_send_and_read_finish(SOUP_SESSION(source_object), res, &error);
    
    if (error) {
        g_task_return_error(log_task->task, g_error_copy(error));
        qrz_log_task_free(log_task);
        return;
    }
    
    if (!response_body) {
        g_task_return_new_error(log_task->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "No response from QRZ server");
        qrz_log_task_free(log_task);
        return;
    }
    
    // Parse JSON response
    const gchar *response_text = g_bytes_get_data(response_body, NULL);
    g_autoptr(JsonParser) parser = json_parser_new();
    
    if (!json_parser_load_from_data(parser, response_text, -1, &error)) {
        g_task_return_new_error(log_task->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Failed to parse QRZ response: %s", error->message);
        qrz_log_task_free(log_task);
        return;
    }
    
    JsonNode *root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root)) {
        g_task_return_new_error(log_task->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "Invalid QRZ response format");
        qrz_log_task_free(log_task);
        return;
    }
    
    JsonObject *obj = json_node_get_object(root);
    
    // Check for success
    if (json_object_has_member(obj, "RESULT") && 
        g_strcmp0(json_object_get_string_member(obj, "RESULT"), "OK") == 0) {
        g_task_return_boolean(log_task->task, TRUE);
    } else {
        const gchar *reason = json_object_has_member(obj, "REASON") ?
            json_object_get_string_member(obj, "REASON") : "Unknown error";
        g_task_return_new_error(log_task->task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "QRZ logging failed: %s", reason);
    }
    
    qrz_log_task_free(log_task);
}

static void
logbook_qrz_log_qso_async(LogbookProvider *provider,
                          LogbookQso *qso,
                          GCancellable *cancellable,
                          GAsyncReadyCallback callback,
                          gpointer user_data)
{
    LogbookQrz *self = LOGBOOK_QRZ(provider);
    
    if (!logbook_qrz_is_configured(provider)) {
        GTask *task = g_task_new(provider, cancellable, callback, user_data);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED,
                               "QRZ logbook not configured - missing API key");
        g_object_unref(task);
        return;
    }
    
    // Create task data
    QrzLogTask *log_task = g_new0(QrzLogTask, 1);
    log_task->qrz = g_object_ref(self);
    log_task->qso = logbook_qso_new();
    log_task->task = g_task_new(provider, cancellable, callback, user_data);
    
    // Copy QSO data
    log_task->qso->callsign = g_strdup(qso->callsign);
    log_task->qso->park_ref = g_strdup(qso->park_ref);
    log_task->qso->mode = g_strdup(qso->mode);
    log_task->qso->frequency_hz = qso->frequency_hz;
    log_task->qso->qso_datetime = qso->qso_datetime ? g_date_time_ref(qso->qso_datetime) : NULL;
    log_task->qso->rst_sent = g_strdup(qso->rst_sent);
    log_task->qso->rst_received = g_strdup(qso->rst_received);
    log_task->qso->comment = g_strdup(qso->comment);
    
    // Format frequency in MHz
    gdouble freq_mhz = qso->frequency_hz / 1000000.0;
    
    // Format date/time
    g_autofree gchar *qso_date = NULL;
    g_autofree gchar *qso_time = NULL;
    if (qso->qso_datetime) {
        qso_date = g_date_time_format(qso->qso_datetime, "%Y-%m-%d");
        qso_time = g_date_time_format(qso->qso_datetime, "%H:%M");
    } else {
        g_autoptr(GDateTime) now = g_date_time_new_now_utc();
        qso_date = g_date_time_format(now, "%Y-%m-%d");
        qso_time = g_date_time_format(now, "%H:%M");
    }
    
    // Build comment with park reference
    g_autofree gchar *full_comment = NULL;
    if (qso->comment && strlen(qso->comment) > 0) {
        full_comment = g_strdup_printf("POTA %s - %s", qso->park_ref, qso->comment);
    } else {
        full_comment = g_strdup_printf("POTA %s", qso->park_ref);
    }
    
    // Create QRZ API request
    g_autoptr(SoupMessage) msg = soup_message_new("POST", "https://logbook.qrz.com/api");
    
    // Build form data
    GString *form_data = g_string_new(NULL);
    g_string_append_printf(form_data, "KEY=%s", self->api_key);
    g_string_append_printf(form_data, "&ACTION=INSERT");
    g_string_append_printf(form_data, "&ADIF=<call:%ld>%s", strlen(qso->callsign), qso->callsign);
    g_string_append_printf(form_data, "<qso_date:8>%s", qso_date);
    g_string_append_printf(form_data, "<time_on:4>%s", qso_time);
    g_string_append_printf(form_data, "<freq:%.6f>%.6f", freq_mhz, freq_mhz);
    g_string_append_printf(form_data, "<mode:%ld>%s", strlen(qso->mode), qso->mode);
    
    if (qso->rst_sent && strlen(qso->rst_sent) > 0) {
        g_string_append_printf(form_data, "<rst_sent:%ld>%s", strlen(qso->rst_sent), qso->rst_sent);
    }
    if (qso->rst_received && strlen(qso->rst_received) > 0) {
        g_string_append_printf(form_data, "<rst_rcvd:%ld>%s", strlen(qso->rst_received), qso->rst_received);
    }
    
    g_string_append_printf(form_data, "<comment:%ld>%s", strlen(full_comment), full_comment);
    g_string_append(form_data, "<eor>");
    
    soup_message_set_request_body_from_bytes(msg, "application/x-www-form-urlencoded",
                                             g_bytes_new_take(g_string_free(form_data, FALSE), form_data->len));
    
    // Send request
    soup_session_send_and_read_async(self->session, msg, G_PRIORITY_DEFAULT,
                                    cancellable, qrz_log_response_cb, log_task);
}

static gboolean
logbook_qrz_log_qso_finish(LogbookProvider *provider,
                           GAsyncResult *result,
                           GError **error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}