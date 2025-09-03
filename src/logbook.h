// logbook.h - Abstract logbook interface for multiple providers
#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "spot.h"

G_BEGIN_DECLS

// Forward declarations
typedef struct _LogbookProvider LogbookProvider;
typedef struct _LogbookProviderClass LogbookProviderClass;

// QSO data structure for logging
typedef struct {
    gchar *callsign;
    gchar *park_ref;
    gchar *mode;
    gint frequency_hz;
    GDateTime *qso_datetime;
    gchar *rst_sent;
    gchar *rst_received;
    gchar *comment;
} LogbookQso;

#define LOGBOOK_TYPE_PROVIDER (logbook_provider_get_type())
G_DECLARE_DERIVABLE_TYPE(LogbookProvider, logbook_provider, LOGBOOK, PROVIDER, GObject)

// Logbook provider interface
struct _LogbookProviderClass {
    GObjectClass parent_class;
    
    // Virtual methods
    gboolean (*is_configured)(LogbookProvider *provider);
    void (*log_qso_async)(LogbookProvider *provider, 
                         LogbookQso *qso,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data);
    gboolean (*log_qso_finish)(LogbookProvider *provider,
                              GAsyncResult *result,
                              GError **error);
    const gchar* (*get_name)(LogbookProvider *provider);
};

// Interface methods
gboolean logbook_provider_is_configured(LogbookProvider *provider);
void logbook_provider_log_qso_async(LogbookProvider *provider,
                                   LogbookQso *qso,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data);
gboolean logbook_provider_log_qso_finish(LogbookProvider *provider,
                                        GAsyncResult *result,
                                        GError **error);
const gchar* logbook_provider_get_name(LogbookProvider *provider);

// QSO data helpers
LogbookQso* logbook_qso_new(void);
void logbook_qso_free(LogbookQso *qso);
LogbookQso* logbook_qso_from_spot(ArtemisSpot *spot, const gchar *rst_sent, const gchar *rst_received);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LogbookQso, logbook_qso_free)

G_END_DECLS