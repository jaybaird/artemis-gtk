#pragma once

#include <gtk/gtk.h>
#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define ARTEMIS_TYPE_SPOT_HISTORY_DIALOG (spot_history_dialog_get_type())
G_DECLARE_FINAL_TYPE(SpotHistoryDialog, spot_history_dialog, ARTEMIS, SPOT_HISTORY_DIALOG, AdwDialog)

SpotHistoryDialog *spot_history_dialog_new(void);
void spot_history_dialog_set_callsign_and_park(SpotHistoryDialog *self, 
                                                const char *callsign, 
                                                const char *park_ref);
void spot_history_dialog_show_loading(SpotHistoryDialog *self);
void spot_history_dialog_show_error(SpotHistoryDialog *self, const char *error_message);
void spot_history_dialog_show_history(SpotHistoryDialog *self, JsonNode *history_data);

G_END_DECLS