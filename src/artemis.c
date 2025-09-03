#include "artemis.h"
#include "adwaita.h"
#include "gio/gio.h"
#include "glib-object.h"
#include "glib.h"

#include "glibconfig.h"
#include "gtk/gtkdropdown.h"
#include "json-glib/json-glib.h"
#include "pota_client.h"
#include "preferences.h"
#include "spot_card.h"

#include <gtk/gtk.h>
#include <libintl.h>
#include <hamlib/rig.h>
#include <stdint.h>
#include <stdio.h>

#include "utils.h"
#include "spot.h"
#include "spot_repo.h"
#include "database.h"
#include "pota_user_cache.h"
#include "status_page.h"
#include "spot_page.h"

GtkWindow *artemis_app_build_ui(ArtemisApp *self, GtkApplication *app);
static void artemis_app_activate(GApplication *app);
static void artemis_app_class_init(ArtemisAppClass *class);
static void artemis_app_init(ArtemisApp* self);
static void artemis_app_dispose(GObject *object);
static void artemis_app_start_connection_monitoring(ArtemisApp *self);
static void artemis_app_stop_connection_monitoring(ArtemisApp *self);

static guint app_signals[N_SIGNALS];

struct _ArtemisApp {
  AdwApplication parent_instance;

  GtkWindow       *window;

  gboolean        spots_update_paused;

  GtkFlowBox      *spots_container;
  GtkBox          *loading_spinner;

  ArtemisSpotRepo *repo;
  
  // Radio connection management
  RIG             *rig;
  gboolean        radio_connected;
  guint           radio_check_source_id; // For periodic connection checks
  gulong          settings_changed_handler; // For settings change monitoring
  GPtrArray       *pages;

  AdwToastOverlay *toast_overlay;

  guint time_source_id;
  unsigned int    seconds_elapsed;
  unsigned int    seconds_to_update;
  
  // Search functionality
  gchar           *search_text;
  
  // Mode filtering
  gchar           *current_mode_filter;
  
  // Pinned spot tracking
  guint           pinned_spot_hash;
};

typedef struct {
  ArtemisApp  *app;
  GWeakRef     time_banner;
  GWeakRef     time_label;
  GWeakRef     time_progress;
} TimeUpdateContext;

typedef struct {
  const char          *band;
  GtkWidget           *flow;
  GtkFilter           *filter;
  GtkFilterListModel  *filtered;
  GtkSortListModel    *sorted;
  GtkScrolledWindow   *scroller;
  StatusPage          *empty;
  gchar               *current_search_text; // cached search text for this view
  gchar               *current_mode_filter; // cached mode filter for this view
} BandView;

static void band_view_update_empty(BandView *bv) {
  guint n = g_list_model_get_n_items(G_LIST_MODEL(bv->sorted));

  gtk_widget_set_visible(GTK_WIDGET(bv->scroller), n > 0);
  gtk_widget_set_visible(GTK_WIDGET(bv->empty), n == 0);
}

static void band_view_free(BandView *view) {
  if (view) {
    g_free(view->current_search_text);
    g_free(view->current_mode_filter);
    g_free(view);
  }
}

// Radio connection task data
typedef struct {
  ArtemisApp *app;
  gint model_id;
  gchar *connection_type;
  gchar *device_path;
  gchar *network_host;
  gint network_port;
  gint baud_rate;
} RadioConnectionData;

static void radio_connection_data_free(RadioConnectionData *data) {
  if (data) {
    g_object_unref(data->app);
    g_free(data->connection_type);
    g_free(data->device_path);
    g_free(data->network_host);
    g_free(data);
  }
}

// Background thread radio connection function
static void radio_connection_thread_func(GTask *task, gpointer source_object, 
                                        gpointer task_data, GCancellable *cancellable)
{
  RadioConnectionData *data = (RadioConnectionData *)task_data;
  RIG *rig = NULL;
  GError *error = NULL;
  
  // Initialize rig
  rig = rig_init(data->model_id);
  if (!rig) {
    g_set_error(&error, G_IO_ERROR, G_IO_ERROR_FAILED, 
                "Failed to initialize radio model %d", data->model_id);
    g_task_return_error(task, error);
    return;
  }
  
  // Configure connection based on type
  if (g_strcmp0(data->connection_type, "serial") == 0 || g_strcmp0(data->connection_type, "usb") == 0) {
    strncpy(rig->state.rigport.pathname, data->device_path, HAMLIB_FILPATHLEN - 1);
    rig->state.rigport.parm.serial.rate = data->baud_rate;
  } else if (g_strcmp0(data->connection_type, "network") == 0) {
    g_snprintf(rig->state.rigport.pathname, HAMLIB_FILPATHLEN, "%s:%d", 
               data->network_host, data->network_port);
  }
  
  // Attempt connection
  int result = rig_open(rig);
  if (result != RIG_OK) {
    g_set_error(&error, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED,
                "Failed to connect to radio: %s", rigerror(result));
    rig_cleanup(rig);
    g_task_return_error(task, error);
    return;
  }
  
  // Success - return the rig
  g_task_return_pointer(task, rig, (GDestroyNotify)rig_cleanup);
}

// Async callback when radio connection completes
static void on_radio_connection_complete(GObject *source, GAsyncResult *result, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  GError *error = NULL;
  
  RIG *rig = g_task_propagate_pointer(G_TASK(result), &error);
  
  if (rig) {
    // Success - store the rig and mark as connected
    self->rig = rig;
    self->radio_connected = TRUE;
    g_debug("Radio connected successfully");
    
    // Start monitoring now that we're connected
    artemis_app_start_connection_monitoring(self);
  } else {
    // Failed - log the error but don't block the app
    g_warning("Radio connection failed: %s", error ? error->message : "Unknown error");
    g_clear_error(&error);
    self->rig = NULL;
    self->radio_connected = FALSE;
  }
}

// Radio connection management functions
static void artemis_app_init_radio_connection_async(ArtemisApp *self)
{
  GSettings *settings = artemis_app_get_settings();
  
  // Check if radio is configured
  g_autofree gchar *connection_type = g_settings_get_string(settings, "radio-connection-type");
  if (g_strcmp0(connection_type, "none") == 0) {
    return; // No radio configured
  }
  
  // Prepare connection data
  RadioConnectionData *data = g_new0(RadioConnectionData, 1);
  data->app = g_object_ref(self);
  data->model_id = g_settings_get_int(settings, "radio-model");
  data->connection_type = g_strdup(connection_type);
  data->device_path = g_settings_get_string(settings, "radio-device");
  data->network_host = g_settings_get_string(settings, "radio-network-host");
  data->network_port = g_settings_get_int(settings, "radio-network-port");
  data->baud_rate = g_settings_get_int(settings, "radio-baud-rate");
  
  // Create and run async task
  GTask *task = g_task_new(self, NULL, on_radio_connection_complete, self);
  g_task_set_task_data(task, data, (GDestroyNotify)radio_connection_data_free);
  g_task_run_in_thread(task, radio_connection_thread_func);
  g_object_unref(task);
  
  g_debug("Radio connection started in background thread");
}

static void artemis_app_disconnect_radio(ArtemisApp *self)
{
  if (self->rig && self->radio_connected) {
    // Safely close the rig connection
    int close_result = rig_close(self->rig);
    if (close_result != RIG_OK) {
      g_warning("Error closing radio connection: %s", rigerror(close_result));
    }
    self->radio_connected = FALSE;
    g_debug("Radio disconnected");
  }
}

static void artemis_app_reconnect_radio_async(ArtemisApp *self)
{
  // Stop monitoring during reconnection
  artemis_app_stop_connection_monitoring(self);
  
  // Disconnect existing connection
  artemis_app_disconnect_radio(self);
  
  // Clean up existing rig
  if (self->rig) {
    rig_cleanup(self->rig);
    self->rig = NULL;
  }
  
  // Re-initialize connection asynchronously
  artemis_app_init_radio_connection_async(self);
}

// Periodic radio connection check
static gboolean radio_connection_check(gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  
  if (!self->rig || !self->radio_connected) {
    return G_SOURCE_CONTINUE; // Keep checking
  }
  
  // Test connection with a simple command - with crash protection
  if (!self->rig) {
    g_debug("Rig is NULL, skipping connection check");
    self->radio_connected = FALSE;
    return G_SOURCE_CONTINUE;
  }
  
  freq_t freq;
  int result = -1;
  
  // Additional validation before calling hamlib
  if (self->rig->caps == NULL || self->rig->state.comm_state != RIG_OK) {
    g_warning("Rig is in invalid state, marking as disconnected");
    self->radio_connected = FALSE;
    return G_SOURCE_CONTINUE;
  }
  
  result = rig_get_freq(self->rig, RIG_VFO_CURR, &freq);
  
  if (result != RIG_OK) {
    g_warning("Radio connection check failed: %s", rigerror(result));
    self->radio_connected = FALSE;
    
    // Try to reconnect automatically
    artemis_app_reconnect_radio_async(self);
  }
  
  return G_SOURCE_CONTINUE;
}

// Settings change monitoring
static void on_radio_settings_changed(GSettings *settings, gchar *key, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  
  // Only reconnect if radio-related settings changed
  if (g_str_has_prefix(key, "radio-")) {
    g_debug("Radio settings changed (%s), reconnecting...", key);
    artemis_app_reconnect_radio_async(self);
  }
}

static void artemis_app_start_connection_monitoring(ArtemisApp *self)
{
  GSettings *settings = artemis_app_get_settings();
  
  // Start periodic connection check (every 30 seconds)
  if (self->radio_check_source_id == 0) {
    self->radio_check_source_id = g_timeout_add_seconds(30, radio_connection_check, self);
  }
  
  // Monitor settings changes
  if (self->settings_changed_handler == 0) {
    self->settings_changed_handler = g_signal_connect(settings, "changed", 
                                                      G_CALLBACK(on_radio_settings_changed), self);
  }
}

static void artemis_app_stop_connection_monitoring(ArtemisApp *self)
{
  // Remove periodic check
  if (self->radio_check_source_id > 0) {
    g_source_remove(self->radio_check_source_id);
    self->radio_check_source_id = 0;
  }
  
  // Remove settings monitoring
  GSettings *settings = artemis_app_get_settings();
  if (self->settings_changed_handler > 0) {
    g_signal_handler_disconnect(settings, self->settings_changed_handler);
    self->settings_changed_handler = 0;
  }
}

static void on_search_changed(ArtemisApp *app, const gchar *search_text, gpointer user_data) {
  BandView *view = (BandView *)user_data;
  
  // Update cached search text
  g_free(view->current_search_text);
  view->current_search_text = g_strdup(search_text);
  
  // Trigger filter refresh
  gtk_filter_changed(view->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

static void on_mode_filter_changed(ArtemisApp *app, const gchar *mode, gpointer user_data) {
  BandView *view = (BandView *)user_data;
  
  // Update cached mode filter
  g_free(view->current_mode_filter);
  view->current_mode_filter = g_strdup(mode);
  
  // Trigger filter refresh
  gtk_filter_changed(view->filter, GTK_FILTER_CHANGE_DIFFERENT);
}

// Update all spot cards in a flow box to check their pinned state
static void update_spot_cards_pinned_state_in_flow(GtkFlowBox *flow) {
  GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index(flow, 0);
  guint index = 0;
  
  while (child) {
    GtkWidget *card_widget = gtk_flow_box_child_get_child(child);
    if (ARTEMIS_IS_SPOT_CARD(card_widget)) {
      spot_card_update_pinned_state(ARTEMIS_SPOT_CARD(card_widget));
    }
    
    index++;
    child = gtk_flow_box_get_child_at_index(flow, index);
  }
}

static void update_spot_cards_hunted_state_in_flow(GtkFlowBox *flow) {
  GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index(flow, 0);
  guint index = 0;
  
  while (child) {
    GtkWidget *card_widget = gtk_flow_box_child_get_child(child);
    if (ARTEMIS_IS_SPOT_CARD(card_widget)) {
      spot_card_update_hunted_state(ARTEMIS_SPOT_CARD(card_widget));
    }
    
    index++;
    child = gtk_flow_box_get_child_at_index(flow, index);
  }
}

// Update all spot cards across all band views
static gboolean artemis_app_update_all_spot_cards_pinned_state(ArtemisApp *self) {
  if (!self->pages) return G_SOURCE_REMOVE;
  
  g_debug("Updating pinned state for all spot cards across %u views", self->pages->len);
  
  for (guint i = 0; i < self->pages->len; i++) {
    BandView *view = g_ptr_array_index(self->pages, i);
    if (view && view->flow) {
      update_spot_cards_pinned_state_in_flow(GTK_FLOW_BOX(view->flow));
      
      // Also trigger sort refresh
      GtkSorter *sorter = gtk_sort_list_model_get_sorter(view->sorted);
      if (sorter) {
        gtk_sorter_changed(sorter, GTK_SORTER_CHANGE_DIFFERENT);
      }
    }
  }
  
  return G_SOURCE_REMOVE; // Remove this idle callback after running once
}

static gboolean artemis_app_update_all_spot_cards_hunted_state(ArtemisApp *self) {
  if (!self->pages) return G_SOURCE_REMOVE;
  
  g_debug("Updating hunted state for all spot cards across %u views", self->pages->len);
  
  for (guint i = 0; i < self->pages->len; i++) {
    BandView *view = g_ptr_array_index(self->pages, i);
    if (view && view->flow) {
      update_spot_cards_hunted_state_in_flow(GTK_FLOW_BOX(view->flow));
    }
  }
  
  return G_SOURCE_REMOVE; // Remove this idle callback after running once
}

static void on_items_changed(GListModel *m, guint pos, guint removed, guint added, gpointer user_data) {
  band_view_update_empty((BandView*)user_data);
}

static void on_highlight_unhunted_parks_changed(GSettings *settings, const gchar *key, gpointer user_data) {
  ArtemisApp *app = ARTEMIS_APP(user_data);
  g_debug("Highlight unhunted parks setting changed - refreshing spot cards");
  g_idle_add((GSourceFunc)artemis_app_update_all_spot_cards_hunted_state, app);
}

// Search helper function - case insensitive substring search
static gboolean contains_text_case_insensitive(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle) return TRUE;
  if (!*haystack) return FALSE;
  
  g_autofree gchar *haystack_lower = g_ascii_strdown(haystack, -1);
  g_autofree gchar *needle_lower = g_ascii_strdown(needle, -1);
  
  return strstr(haystack_lower, needle_lower) != NULL;
}

static gboolean combined_filter_func(gpointer item, gpointer user_data)
{
  BandView *view = (BandView *)user_data;
  ArtemisSpot *spot = ARTEMIS_SPOT(item);
  
  // Apply band filter first (if not "All")
  if (view->band && g_strcmp0(view->band, "All") != 0) {
    if (g_strcmp0(artemis_spot_get_band(spot), view->band) != 0) {
      return FALSE;
    }
  }
  
  // Apply mode filter (if not "All")
  const char *mode_filter = view->current_mode_filter;
  if (mode_filter && *mode_filter && g_strcmp0(mode_filter, "All") != 0) {
    if (g_strcmp0(artemis_spot_get_mode(spot), mode_filter) != 0) {
      return FALSE;
    }
  }
  
  // Apply search filter
  const char *search_text = view->current_search_text;
  if (search_text && *search_text) {
    // Search in activator callsign
    if (contains_text_case_insensitive(artemis_spot_get_callsign(spot), search_text)) {
      return TRUE;
    }
    
    // Search in park reference
    if (contains_text_case_insensitive(artemis_spot_get_park_ref(spot), search_text)) {
      return TRUE;
    }
    
    // Search in park name
    if (contains_text_case_insensitive(artemis_spot_get_park_name(spot), search_text)) {
      return TRUE;
    }
    
    // No match found in any search field
    return FALSE;
  }
  
  // No search text, so item passes search filter
  return TRUE;
}

static gboolean is_spot_pinned(ArtemisSpot *spot, ArtemisApp *app)
{
  return (app->pinned_spot_hash == hash_spot(spot));
}

static int pinned_spot_sort_func(gconstpointer a, gconstpointer b, gpointer user_data)
{
  ArtemisSpot *spot_a = ARTEMIS_SPOT((gpointer)a);
  ArtemisSpot *spot_b = ARTEMIS_SPOT((gpointer)b);
  
  // Get the default app
  ArtemisApp *app = ARTEMIS_APP(g_application_get_default());
  
  gboolean a_is_pinned = is_spot_pinned(spot_a, app);
  gboolean b_is_pinned = is_spot_pinned(spot_b, app);
  
  // If spot_a is pinned and spot_b is not, spot_a comes first
  if (a_is_pinned && !b_is_pinned) {
    return -1;
  }
  
  // If spot_b is pinned and spot_a is not, spot_b comes first  
  if (b_is_pinned && !a_is_pinned) {
    return 1;
  }
  
  // If both are pinned or both are not pinned, maintain original order
  return 0;
}

static GtkWidget *create_spotcard_cb(gpointer item, gpointer user_data) {
  return spot_card_new_from_spot(item);
}

static BandView *add_band_page(AdwViewStack *stack, GListModel *base, const char *band_label, const char *icon_name, ArtemisApp *app)
{
  BandView *view = g_new0(BandView, 1);

  view->band = band_label;
  view->current_search_text = NULL; // Initialize search text
  view->current_mode_filter = NULL; // Initialize mode filter
  
  // Create filter model
  view->filter = GTK_FILTER(gtk_custom_filter_new(combined_filter_func, view, NULL));
  view->filtered = gtk_filter_list_model_new(base, view->filter);
  
  // Create sort model on top of filter model
  GtkSorter *sorter = GTK_SORTER(gtk_custom_sorter_new(pinned_spot_sort_func, view, NULL));
  view->sorted = gtk_sort_list_model_new(G_LIST_MODEL(view->filtered), sorter);

  view->flow = gtk_flow_box_new();
  gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(view->flow), 6);
  gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(view->flow), 12);
  gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(view->flow), GTK_SELECTION_NONE);
  gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(view->flow), 4);
  gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(view->flow), FALSE);
  gtk_flow_box_bind_model(
    GTK_FLOW_BOX(view->flow), 
    G_LIST_MODEL(view->sorted), 
    create_spotcard_cb, 
    NULL, 
    NULL);

  view->scroller = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
  gtk_scrolled_window_set_child(view->scroller, view->flow);
  gtk_widget_set_hexpand(GTK_WIDGET(view->scroller), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(view->scroller), TRUE);

  view->empty = status_page_new();
  status_page_set_icon_name(view->empty, icon_name);

  status_page_set_title(view->empty,
                          view->band ? g_strdup_printf(_("No %s spots"), view->band) : _("No spots"));
  status_page_set_description(view->empty,
                          view->band ? g_strdup_printf(_("There are no current spots on %s."), view->band)
                                   : _("There are no current spots"));

  gtk_widget_set_visible(GTK_WIDGET(view->empty), FALSE);
  
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_vexpand(box, TRUE);
  gtk_widget_set_hexpand(box, TRUE);

  gtk_box_append(GTK_BOX(box), GTK_WIDGET(view->scroller));
  gtk_box_append(GTK_BOX(box), GTK_WIDGET(view->empty));

  AdwViewStackPage *page = adw_view_stack_add_titled(
    stack, 
    box, 
    band_label ? band_label : "All",
    band_label ? band_label : "All"
  );

  if (icon_name) adw_view_stack_page_set_icon_name(page, icon_name);

  g_signal_connect(view->sorted, "items-changed", G_CALLBACK(on_items_changed), view);
  g_signal_connect(app, "search-changed", G_CALLBACK(on_search_changed), view);
  g_signal_connect(app, "mode-filter-changed", G_CALLBACK(on_mode_filter_changed), view);
  band_view_update_empty(view);

  return view;
}

void build_band_stack(AdwViewStack *stack, ArtemisSpotRepo *repo, ArtemisApp *app, GPtrArray **out_pages) {
  GListModel *base = artemis_spot_repo_get_model(repo); // borrowed

  GPtrArray *pages = g_ptr_array_new_with_free_func((GDestroyNotify)band_view_free);
  for (guint i = 0; i < G_N_ELEMENTS(BANDS); ++i) {
    BandView *bv = add_band_page(stack, base, BANDS[i], g_strdup_printf("band-%s", BANDS[i]), app);
    g_ptr_array_add(pages, bv);
  }
  if (out_pages) *out_pages = pages;
}

G_DEFINE_TYPE(ArtemisApp, artemis_app, ADW_TYPE_APPLICATION)

static void on_repo_busy(ArtemisSpotRepo *repo, gboolean busy, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  gtk_widget_set_visible(GTK_WIDGET(self->loading_spinner), busy);
}

static void on_repo_refreshed(ArtemisSpotRepo *repo, guint n, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  AdwToast *toast = adw_toast_new("");

  const char *fmt = ngettext("%u spot refreshed", "%u spots refreshed", n);
  g_autofree char *title = g_strdup_printf(fmt, n);

  adw_toast_set_title(toast, title);
  adw_toast_set_timeout(toast, 5);
  adw_toast_overlay_add_toast(self->toast_overlay, toast);
  
  // Reapply pinned styles after refresh since we have new spot objects
  artemis_app_update_all_spot_cards_pinned_state(self); // Call directly, not as idle callback
}

static void on_repo_error(ArtemisSpotRepo *repo, GError *error, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  AdwDialog *dialog = adw_alert_dialog_new(_("Unable to refresh spots"), NULL);

  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog),
                              _("Unable to refresh spots due to an error: %s"), error->message);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                "cancel",  _("_Cancel"),
                                "retry", _("_Retry"),
                                NULL);
  adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "cancel");
  adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

  //g_signal_connect (dialog, "response", G_CALLBACK (response_cb), self);

  adw_dialog_present(dialog, GTK_WIDGET(self->window));
}

static void artemis_app_activate(GApplication *app)
{
  ArtemisApp *self = ARTEMIS_APP(app);
  self->window = artemis_app_build_ui(self, GTK_APPLICATION(app));
  gtk_window_present(self->window);
}

static void on_search_entry_changed(GtkSearchEntry *entry, gpointer user_data) {
  ArtemisApp *app = ARTEMIS_APP(user_data);
  const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
  
  // Update app's search text
  g_clear_pointer(&app->search_text, g_free);
  app->search_text = g_strdup(text);
  
  // Emit signal to notify all band views
  artemis_app_emit_search_changed(app, text);
}

static void on_mode_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
  ArtemisApp *app = ARTEMIS_APP(user_data);
  GtkDropDown *dropdown = GTK_DROP_DOWN(object);
  unsigned int index = gtk_drop_down_get_selected(dropdown);
  const char *value = gtk_string_list_get_string(
      GTK_STRING_LIST(gtk_drop_down_get_model(dropdown)), index);

  // Update app's mode filter
  g_clear_pointer(&app->current_mode_filter, g_free);
  app->current_mode_filter = g_strdup(value);

  // Emit signal to notify all band views
  artemis_app_emit_mode_filter_changed(app, value);
}

static void on_hide_qrt_changed(GtkSwitch *hide_qrt, gpointer user_data)
{
  g_message("hide qrt changed");
}

static void on_hide_hunted_changed(GtkSwitch *hide_hunted, gpointer user_data)
{
  g_message("hide hunted changed");
}

static void spot_submitted_callback(GObject *src,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
  PotaClient *client = ARTEMIS_POTA_CLIENT(src);
  g_return_if_fail(client != NULL);
  ArtemisApp *self = ARTEMIS_APP(user_data);

  GError *error = NULL;
  JsonNode *node = pota_client_post_spot_finish(client, result, &error);
  gchar *s = json_to_string(node, TRUE);   // TRUE = pretty-print
  g_print("%s\n", s);
  g_free(s);
  const gchar *message = NULL;

  if (error != NULL) {
    message = error->message ? error->message : _("Unknown error");
    goto alert;
  }

  if (node == NULL) {
    message = _("No response from server.");
    goto alert;
  }

  if (JSON_NODE_HOLDS_ARRAY(node)) {
    JsonArray *arr = json_node_get_array(node);
    guint array_length = json_array_get_length(arr);
    
    if (array_length >= 1) {
      // Get user's callsign from preferences to identify their spot
      GSettings *settings = artemis_app_get_settings();
      g_autofree gchar *user_callsign = g_settings_get_string(settings, "callsign");
      
      ArtemisSpot *user_spot = NULL;
      
      // Iterate through all spots to find the one submitted by the user
      for (guint i = 0; i < array_length; i++) {
        ArtemisSpot *spot = artemis_spot_new_from_json(json_array_get_object_element(arr, i));
        if (spot == NULL) {
          continue; // Skip invalid spots
        }
        
        const char *spotter = artemis_spot_get_spotter(spot);
        if (spotter && user_callsign && g_strcmp0(spotter, user_callsign) == 0) {
          // Found the user's spot
          user_spot = spot;
          break;
        }
        
        g_object_unref(spot);
      }

      sqlite3_int64 qso_id = 0;
      GError *db_err = NULL;
      if (!spot_db_add_qso_from_spot(spot_db_get_instance(), user_spot, &qso_id, &db_err)) {
        message = db_err && db_err->message ? db_err->message : _("Failed to write QSO to database.");
        g_clear_error(&db_err);
        g_object_unref(user_spot);
        goto alert;
      }

      g_object_unref(user_spot);
      if (node) json_node_unref(node);

      artemis_spot_repo_update_spots(self->repo, 60);
      return;
    }
  }

  // Unexpected JSON type
  message = _("Unexpected response type from server.");

alert:
  {
    const char *fmt = _("Unable to spot due to the following error: %s");
    g_autofree gchar *body = g_strdup_printf(fmt, message ? message : _("Unknown error"));

    AdwAlertDialog *dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Unable to Spot"), body));
    adw_alert_dialog_add_response(dlg, "ok", _("_OK"));
    adw_alert_dialog_set_default_response(dlg, "ok");
    adw_alert_dialog_set_close_response(dlg, "ok");

    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(self->window));
  }

  if (node) json_node_unref(node);
  if (error) g_error_free(error);
}

static void on_spot_submitted(ArtemisApp *app, ArtemisSpot *spot, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(app);
  
  self->pinned_spot_hash = G_MAXUINT;
  
  // Update visual state on the next idle cycle to ensure all signal handlers have run
  g_idle_add((GSourceFunc)artemis_app_update_all_spot_cards_pinned_state, self);
  
  PotaClient *client = artemis_spot_repo_get_pota_client(app->repo);
  pota_client_post_spot_async(client, spot, NULL, spot_submitted_callback, app);
}

static void on_tune_frequency(ArtemisApp *app, guint64 frequency_khz, ArtemisSpot *spot, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  
  guint spot_hash = hash_spot(spot);
  if (self->pinned_spot_hash == spot_hash)
  {
    self->pinned_spot_hash = G_MAXUINT;
    g_debug("Pinned spot unset");
    g_idle_add((GSourceFunc)artemis_app_update_all_spot_cards_pinned_state, self);
    return; // bail because we want to unset
  }
  else 
  {
    self->pinned_spot_hash = spot_hash;
    g_debug("Pinned spot set");
  }
  
  // Update visual state on the next idle cycle to ensure all signal handlers have run
  g_idle_add((GSourceFunc)artemis_app_update_all_spot_cards_pinned_state, self);
  
  // Check if radio is connected, if not we updated the pinned state to "track" or reset and now we bail
  if (!self->rig || !self->radio_connected) {
    return;
  }
  
  freq_t freq_hz = (freq_t)(frequency_khz * 1000); // Convert MHz to Hz
  int set_result = -1;
  
  // Additional validation before calling hamlib
  if (!self->rig || self->rig->caps == NULL) {
    g_warning("Rig is NULL or has invalid caps, cannot set frequency");
    // Show error dialog
    AdwDialog *alert = adw_alert_dialog_new(_("Radio Error"), _("Radio is not properly initialized. Please check your radio settings."));
    adw_alert_dialog_add_response(ADW_ALERT_DIALOG(alert), "ok", _("OK"));
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(alert), "ok");
    adw_dialog_present(alert, GTK_WIDGET(self->window));
    return;
  }
  
  set_result = rig_set_freq(self->rig, RIG_VFO_CURR, freq_hz);
  
  if (set_result == RIG_OK) {
    // Success - show toast
    g_autofree gchar *msg = g_strdup_printf("Tuned radio to %.3f MHz", frequency_khz / 1000.0f);
    AdwToast *toast = adw_toast_new(msg);
    adw_toast_overlay_add_toast(self->toast_overlay, toast);
  } else {
    // Error setting frequency - try to reconnect and retry once
    g_warning("Radio frequency setting failed: %s, attempting reconnect", rigerror(set_result));
    artemis_app_reconnect_radio_async(self);
    
    if (self->rig && self->radio_connected && self->rig->caps) {
      set_result = rig_set_freq(self->rig, RIG_VFO_CURR, freq_hz);
      if (set_result == RIG_OK) {
        g_autofree gchar *msg = g_strdup_printf("Tuned radio to %.3f MHz (after reconnect)", frequency_khz / 1000.0f);
        AdwToast *toast = adw_toast_new(msg);
        adw_toast_overlay_add_toast(self->toast_overlay, toast);
        return;
      }
    }
    
    // Still failed - show error dialog
    g_autofree gchar *error_detail = g_strdup_printf("Failed to set frequency: %s\n\nPlease verify your radio is responding correctly and try again.", rigerror(set_result));
    AdwAlertDialog *alert = ADW_ALERT_DIALOG(adw_alert_dialog_new("Frequency Setting Failed", error_detail));
    adw_alert_dialog_add_response(alert, "ok", "OK");
    adw_alert_dialog_set_default_response(alert, "ok");
    adw_dialog_present(ADW_DIALOG(alert), GTK_WIDGET(self->window));
  }
}

static void artemis_app_class_init(ArtemisAppClass *klass)
{
  G_APPLICATION_CLASS(klass)->activate = artemis_app_activate;
  G_OBJECT_CLASS(klass)->dispose = artemis_app_dispose;

  app_signals[SIGNAL_SPOT_SUBMITTED] = g_signal_new("spot-submitted",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    1,
    ARTEMIS_TYPE_SPOT
  );

  app_signals[SIGNAL_SEARCH_CHANGED] = g_signal_new("search-changed",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    1,
    G_TYPE_STRING
  );

  app_signals[SIGNAL_MODE_FILTER_CHANGED] = g_signal_new("mode-filter-changed",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    1,
    G_TYPE_STRING
  );

  app_signals[SIGNAL_TUNE_FREQUENCY] = g_signal_new("tune-frequency",
    G_TYPE_FROM_CLASS(klass),
    G_SIGNAL_RUN_LAST,
    0,
    NULL, NULL,
    NULL,
    G_TYPE_NONE,
    2,
    G_TYPE_UINT64, ARTEMIS_TYPE_SPOT
  );
}

static void on_banner_button_clicked(AdwBanner *banner, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  self->spots_update_paused = !self->spots_update_paused;

  if (self->spots_update_paused)
  {
    adw_banner_set_button_label(banner, "Resume");
    self->seconds_elapsed = 0;
  }
  else
  {
    adw_banner_set_button_label(banner, "Pause");
    artemis_spot_repo_update_spots(self->repo, 60);
  }
}

static void on_add_button_clicked(GtkButton *button, gpointer user_data)
{
  show_add_spot_page(GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button))));
}

static gboolean update_time(gpointer user_data)
{
  TimeUpdateContext *ctx = (TimeUpdateContext *)user_data;

  GtkLabel       *label   = GTK_LABEL(g_weak_ref_get(&ctx->time_label));
  GtkProgressBar *prog    = GTK_PROGRESS_BAR(g_weak_ref_get(&ctx->time_progress));
  AdwBanner      *banner  = ADW_BANNER(g_weak_ref_get(&ctx->time_banner));

  // If the UI is gone, stop the timeout cleanly
  if (!label || !prog || !banner) {
    if (label)  g_object_unref(label);
    if (prog)   g_object_unref(prog);
    if (banner) g_object_unref(banner);
    return G_SOURCE_REMOVE;
  }

  GDateTime *now = g_date_time_new_now_utc();
  char *formatted = g_date_time_format(now, "%H:%M:%S UTC");

  gtk_label_set_text(label, formatted);

  if (!ctx->app->spots_update_paused)
  {
    ctx->app->seconds_elapsed += 1;
    if (ctx->app->seconds_elapsed >= ctx->app->seconds_to_update)
    {
      artemis_spot_repo_update_spots(ctx->app->repo, 60);
    }
    ctx->app->seconds_elapsed = ctx->app->seconds_elapsed % ctx->app->seconds_to_update;

    gtk_progress_bar_set_fraction(prog, (gfloat)ctx->app->seconds_elapsed / (gfloat)ctx->app->seconds_to_update);

    char *message = g_strdup_printf("Spots will refresh in %d seconds", ctx->app->seconds_to_update - ctx->app->seconds_elapsed);
    adw_banner_set_title(banner, message);
    g_free(message);
  }
  else
  {
    gtk_progress_bar_set_fraction(prog, 0.f);
    adw_banner_set_title(banner, "");
  }

  g_date_time_unref(now);
  g_free(formatted);

  return G_SOURCE_CONTINUE;
}

void setup_spots_updater(ArtemisApp *self, GtkBuilder* builder)
{
  GtkWidget *banner = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_banner"));
  if (banner == NULL)
  {
      g_warning("Adding spots updater failed because there was no banner!");
  }   

  g_signal_connect(banner, "button-clicked", G_CALLBACK(on_banner_button_clicked), self);
}

void setup_time_updater(ArtemisApp *self, GtkBuilder* builder)
{
  GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "current_time"));
  GtkWidget *progress_bar = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_progress"));
  GtkWidget *banner = GTK_WIDGET(gtk_builder_get_object(builder, "refresh_banner"));
  
  if (!label || !progress_bar || !banner) return;
  
  TimeUpdateContext *ctx = g_new(TimeUpdateContext, 1);
      ctx->app = self;
      g_weak_ref_init(&ctx->time_label,    label);
      g_weak_ref_init(&ctx->time_progress, progress_bar);
      g_weak_ref_init(&ctx->time_banner,   banner);

      update_time(ctx);

      self->time_source_id = g_timeout_add_seconds_full(G_PRIORITY_DEFAULT, 1, update_time, ctx, (GDestroyNotify)g_free);
}

GtkWindow *artemis_app_build_ui(ArtemisApp *self, GtkApplication *app) {
  adw_init();

  g_type_ensure(ARTEMIS_TYPE_SPOT_CARD);
  g_type_ensure(ARTEMIS_TYPE_SPOT);
  g_type_ensure(ARTEMIS_TYPE_STATUS_PAGE);

  AdwStyleManager *manager = adw_style_manager_get_default();
  adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_PREFER_DARK);

  GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
  gtk_icon_theme_add_resource_path(theme, "/com/k0vcz/artemis/icons/hicolor");
  gtk_icon_theme_add_resource_path(theme, "/com/k0vcz/artemis/icons");

  GtkBuilder *builder = gtk_builder_new();
  GtkBuilderScope *scope = gtk_builder_cscope_new();

  gtk_builder_cscope_add_callback_symbol(GTK_BUILDER_CSCOPE(scope), "on_add_button_clicked", G_CALLBACK(on_add_button_clicked));
  gtk_builder_cscope_add_callback_symbol(GTK_BUILDER_CSCOPE(scope), "on_hide_hunted_changed", G_CALLBACK(on_hide_hunted_changed));
  gtk_builder_cscope_add_callback_symbol(GTK_BUILDER_CSCOPE(scope), "on_hide_qrt_changed", G_CALLBACK(on_hide_qrt_changed));
  gtk_builder_set_scope(builder, scope);
  gtk_builder_add_from_resource(builder, RESOURCE_PATH "ui/main_window.ui", NULL);

  g_signal_connect(self->repo, "busy-changed",  G_CALLBACK(on_repo_busy),       self);
  g_signal_connect(self->repo, "refreshed",     G_CALLBACK(on_repo_refreshed),  self);
  g_signal_connect(self->repo, "error",         G_CALLBACK(on_repo_error),      self);

  GtkCssProvider* provider = gtk_css_provider_new();
  gtk_css_provider_load_from_resource(provider, RESOURCE_PATH "css/style.css");
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_set_application_name("Artemis");
  GtkWindow *win = GTK_WINDOW(gtk_builder_get_object(builder, "window"));
  gtk_window_set_application(win, app);
  gtk_window_set_title(win, "Artemis");

  self->spots_container = GTK_FLOW_BOX(gtk_builder_get_object(builder, "spots_container"));
  self->loading_spinner = GTK_BOX(gtk_builder_get_object(builder, "loading_spinner"));
  self->toast_overlay = ADW_TOAST_OVERLAY(gtk_builder_get_object(builder, "toast_overlay"));

  AdwViewStack *stack = ADW_VIEW_STACK(gtk_builder_get_object(builder, "band_stack"));
  g_assert(stack);
  GPtrArray *pages = NULL;
  build_band_stack(stack, self->repo, self, &pages);
  self->pages = pages;

  setup_time_updater(self, builder);
  setup_spots_updater(self, builder);

  // Connect search entry signal manually to pass app as user_data
  GtkWidget *search_entry = GTK_WIDGET(gtk_builder_get_object(builder, "search_entry"));
  if (search_entry) {
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_entry_changed), self);
  }

  // Connect mode dropdown signal manually to pass app as user_data
  GtkWidget *mode_dropdown = GTK_WIDGET(gtk_builder_get_object(builder, "search_select"));
  if (mode_dropdown) {
    g_signal_connect(mode_dropdown, "notify::selected", G_CALLBACK(on_mode_changed), self);
  }

  g_object_unref(builder);
  g_object_unref(scope);
  g_object_unref(provider);

  artemis_spot_repo_update_spots(self->repo, 60);

  g_signal_connect(app, "spot-submitted", G_CALLBACK(on_spot_submitted), NULL);
  g_signal_connect(app, "tune-frequency", G_CALLBACK(on_tune_frequency), self);

  // Initialize radio connection if configured
  artemis_app_init_radio_connection_async(self);

  return win;
}

static void
artemis_app_about_action(GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
  ArtemisApp *self = user_data;
  GtkWindow *window = gtk_application_get_active_window (GTK_APPLICATION (self));

  adw_show_about_dialog(GTK_WIDGET(window),
                         "application-name", "Artemis — POTA Hunter",
                         "application-icon", APPLICATION_ID,
                         "developer-name", "Jay Baird",
                         "version", g_strdup_printf("%d.%d.%d", 
                                VERSION_MAJOR(APP_VERSION),
                                VERSION_MINOR(APP_VERSION),
                                VERSION_PATCH(APP_VERSION)),
                         "copyright", "© 2025 Jay Baird",
                         NULL);
}

static void
artemis_app_quit_action (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  ArtemisApp *self = user_data;
  g_application_quit(G_APPLICATION (self));
}

static void artemis_app_preferences_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
  ArtemisApp *self = user_data;
  GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION (self));
  show_preferences_dialog(GTK_WIDGET(window));
}

static const GActionEntry app_actions[] = {
  { "quit", artemis_app_quit_action },
  { "about", artemis_app_about_action },
  {"preferences", artemis_app_preferences_action},
};

static void artemis_app_dispose(GObject *object) {
  ArtemisApp *self = ARTEMIS_APP(object);

  if (self->time_source_id) {
    g_source_remove(self->time_source_id);
    self->time_source_id = 0;
  }

  // Stop radio connection monitoring
  artemis_app_stop_connection_monitoring(self);

  // Cleanup radio connection with crash protection
  if (self->rig) {
    if (self->radio_connected) {
      int close_result = rig_close(self->rig);
      if (close_result != RIG_OK) {
        g_warning("Error closing radio during cleanup: %s", rigerror(close_result));
      }
    }
    
    // Safely cleanup the rig
    int cleanup_result = rig_cleanup(self->rig);
    if (cleanup_result != RIG_OK) {
      g_warning("Error during rig cleanup: %s", rigerror(cleanup_result));
    }
    
    self->rig = NULL;
  }
  self->radio_connected = FALSE;

  g_clear_pointer(&self->search_text, g_free);
  g_clear_pointer(&self->current_mode_filter, g_free);

  g_ptr_array_unref(self->pages);
  
  // Cleanup singleton instances
  spot_db_cleanup_instance();
  artemis_pota_user_cache_cleanup_instance();

  G_OBJECT_CLASS(artemis_app_parent_class)->dispose(object);
}

static void on_update_interval_changed(GSettings *s, const char *key, gpointer user_data)
{
  ArtemisApp *self = ARTEMIS_APP(user_data);
  int secs = g_settings_get_int (s, "update-interval");

  self->seconds_to_update = secs;
  self->seconds_elapsed = 0;
}

static void artemis_app_init(ArtemisApp* self) 
{
  GSettings *settings = artemis_app_get_settings();
  // Database is now singleton - no need to create instance here
  self->seconds_to_update = g_settings_get_int(settings, "update-interval");

  self->repo = artemis_spot_repo_new();
  
  // Initialize radio monitoring fields
  self->radio_check_source_id = 0;
  self->settings_changed_handler = 0;
  
  // Initialize pinned spot
  self->pinned_spot_hash = G_MAXUINT;

  g_action_map_add_action_entries (G_ACTION_MAP(self),
                                  app_actions,
                                  G_N_ELEMENTS(app_actions),
                                  self);
  gtk_application_set_accels_for_action (GTK_APPLICATION(self),
                                      "app.quit",
                                      (const char *[]) { "<primary>q", NULL });
  gtk_application_set_accels_for_action (GTK_APPLICATION(self),
                    "app.preferences",
                                  (const char *[]) { "<primary>comma", NULL });
  gtk_application_set_accels_for_action (
    GTK_APPLICATION (self),
    "app.about",
    (const char *[]) { "F1", NULL }
);
  

  g_signal_connect(artemis_app_get_settings(), "changed::update-interval",
                  G_CALLBACK(on_update_interval_changed), self);
  g_signal_connect(artemis_app_get_settings(), "changed::highlight-unhunted-parks",
                  G_CALLBACK(on_highlight_unhunted_parks_changed), self);
}

GSettings *artemis_app_get_settings()
{
  static GSettings *settings = NULL;
  if (!settings)
  {
    settings = g_settings_new(SCHEMA_ID);
  }

  return settings;
}

void artemis_app_emit_spot_submitted(ArtemisApp *app, ArtemisSpot *spot)
{
  g_signal_emit(app, app_signals[SIGNAL_SPOT_SUBMITTED], 0, spot);
}

void artemis_app_emit_search_changed(ArtemisApp *app, const gchar *search_text)
{
  g_signal_emit(app, app_signals[SIGNAL_SEARCH_CHANGED], 0, search_text);
}

void artemis_app_emit_mode_filter_changed(ArtemisApp *app, const gchar *mode)
{
  g_signal_emit(app, app_signals[SIGNAL_MODE_FILTER_CHANGED], 0, mode);
}

void artemis_app_emit_tune_frequency(ArtemisApp *app, guint64 frequency_khz, ArtemisSpot *spot)
{
  g_signal_emit(app, app_signals[SIGNAL_TUNE_FREQUENCY], 0, frequency_khz, spot);
}

ArtemisSpot *artemis_app_get_pinned_spot(ArtemisApp *app)
{
  g_return_val_if_fail(ARTEMIS_IS_APP(app), NULL);
  
  if (app->pinned_spot_hash == G_MAXUINT) {
    return NULL;
  }
  
  // Search through the spot model to find the matching spot
  GListModel *model = artemis_spot_repo_get_model(app->repo);
  guint n_items = g_list_model_get_n_items(model);
  
  for (guint i = 0; i < n_items; i++) {
    ArtemisSpot *spot = g_list_model_get_item(model, i);
    if (is_spot_pinned(spot, app)) {
      return spot; // Caller takes ownership
    }
    g_object_unref(spot);
  }
  
  return NULL;
}

ArtemisSpotRepo *artemis_app_get_spot_repo(ArtemisApp *app)
{
  g_return_val_if_fail(ARTEMIS_IS_APP(app), NULL);
  return app->repo;
}

gboolean artemis_app_is_rig_connected(ArtemisApp *app)
{
  return app->rig && app->radio_connected;
}