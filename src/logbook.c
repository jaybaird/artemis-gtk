// logbook.c - Abstract logbook interface implementation
#include "logbook.h"

G_DEFINE_ABSTRACT_TYPE(LogbookProvider, logbook_provider, G_TYPE_OBJECT)

static void
logbook_provider_class_init(LogbookProviderClass *klass)
{
}

static void
logbook_provider_init(LogbookProvider *self)
{
}

gboolean
logbook_provider_is_configured(LogbookProvider *provider)
{
  g_return_val_if_fail(LOGBOOK_IS_PROVIDER(provider), FALSE);
  
  LogbookProviderClass *klass = LOGBOOK_PROVIDER_GET_CLASS(provider);
  if (klass->is_configured) {
    return klass->is_configured(provider);
  }
  
  return FALSE;
}

void
logbook_provider_log_qso_async(LogbookProvider *provider,
                               LogbookQso *qso,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
  g_return_if_fail(LOGBOOK_IS_PROVIDER(provider));
  g_return_if_fail(qso != NULL);
  
  LogbookProviderClass *klass = LOGBOOK_PROVIDER_GET_CLASS(provider);
  if (klass->log_qso_async) {
    klass->log_qso_async(provider, qso, cancellable, callback, user_data);
  } else {
    GTask *task = g_task_new(provider, cancellable, callback, user_data);
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                              "Logbook provider does not implement log_qso_async");
    g_object_unref(task);
  }
}

gboolean
logbook_provider_log_qso_finish(LogbookProvider *provider,
                                GAsyncResult *result,
                                GError **error)
{
  g_return_val_if_fail(LOGBOOK_IS_PROVIDER(provider), FALSE);
  g_return_val_if_fail(G_IS_ASYNC_RESULT(result), FALSE);
    
  LogbookProviderClass *klass = LOGBOOK_PROVIDER_GET_CLASS(provider);
  if (klass->log_qso_finish) {
      return klass->log_qso_finish(provider, result, error);
  }
    
  return g_task_propagate_boolean(G_TASK(result), error);
}

const gchar*
logbook_provider_get_name(LogbookProvider *provider)
{
  g_return_val_if_fail(LOGBOOK_IS_PROVIDER(provider), NULL);
    
  LogbookProviderClass *klass = LOGBOOK_PROVIDER_GET_CLASS(provider);
  if (klass->get_name) {
    return klass->get_name(provider);
  }
    
  return "Unknown";
}

// QSO data helpers
LogbookQso*
logbook_qso_new(void)
{
  LogbookQso *qso = g_new0(LogbookQso, 1);
  return qso;
}

void
logbook_qso_free(LogbookQso *qso)
{
  if (!qso) return;
  
  g_free(qso->callsign);
  g_free(qso->park_ref);
  g_free(qso->mode);
  g_free(qso->rst_sent);
  g_free(qso->rst_received);
  g_free(qso->comment);
  if (qso->qso_datetime) {
    g_date_time_unref(qso->qso_datetime);
  }
  g_free(qso);
}

LogbookQso*
logbook_qso_from_spot(ArtemisSpot *spot, const gchar *rst_sent, const gchar *rst_received)
{
  g_return_val_if_fail(spot != NULL, NULL);
  
  LogbookQso *qso = logbook_qso_new();
  qso->callsign = g_strdup(artemis_spot_get_callsign(spot));
  qso->park_ref = g_strdup(artemis_spot_get_park_ref(spot));
  qso->mode = g_strdup(artemis_spot_get_mode(spot));
  qso->frequency_hz = artemis_spot_get_frequency_hz(spot);
  qso->qso_datetime = g_date_time_new_now_utc();
  qso->rst_sent = g_strdup(rst_sent);
  qso->rst_received = g_strdup(rst_received);
  qso->comment = g_strdup(artemis_spot_get_spotter_comment(spot));
  
  return qso;
}