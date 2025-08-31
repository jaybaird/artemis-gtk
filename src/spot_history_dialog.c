#include "spot_history_dialog.h"
#include "glib-object.h"
#include "glib.h"
#include <glib/gi18n.h>
#include <json-glib/json-glib.h>

struct _SpotHistoryDialog {
  AdwDialog parent_instance;

  AdwWindowTitle *title_widget;
  AdwStatusPage *loading_page;
  GtkScrolledWindow *history_scroll;
  GtkListBox *history_list;
  AdwStatusPage *error_page;

  char *callsign;
  char *park_ref;
};

G_DEFINE_FINAL_TYPE(SpotHistoryDialog, spot_history_dialog, ADW_TYPE_DIALOG)

static void spot_history_dialog_dispose(GObject *gobject)
{
  SpotHistoryDialog *self = ARTEMIS_SPOT_HISTORY_DIALOG(gobject);
  
  g_clear_pointer(&self->callsign, g_free);
  g_clear_pointer(&self->park_ref, g_free);

  G_OBJECT_CLASS(spot_history_dialog_parent_class)->dispose(gobject);
}

static void on_close_clicked(GtkButton *button, gpointer user_data)
{
  SpotHistoryDialog *self = ARTEMIS_SPOT_HISTORY_DIALOG(user_data);
  adw_dialog_close(ADW_DIALOG(self));
}

static void spot_history_dialog_class_init(SpotHistoryDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = spot_history_dialog_dispose;

  gtk_widget_class_set_template_from_resource(widget_class, "/com/k0vcz/artemis/data/ui/spot_history_dialog.ui");
  
  gtk_widget_class_bind_template_child(widget_class, SpotHistoryDialog, title_widget);
  gtk_widget_class_bind_template_child(widget_class, SpotHistoryDialog, loading_page);
  gtk_widget_class_bind_template_child(widget_class, SpotHistoryDialog, history_scroll);
  gtk_widget_class_bind_template_child(widget_class, SpotHistoryDialog, history_list);
  gtk_widget_class_bind_template_child(widget_class, SpotHistoryDialog, error_page);

  gtk_widget_class_bind_template_callback(widget_class, on_close_clicked);
}

static void spot_history_dialog_init(SpotHistoryDialog *self)
{
  gtk_widget_init_template(GTK_WIDGET(self));
}

SpotHistoryDialog *spot_history_dialog_new(void)
{
  return g_object_new(ARTEMIS_TYPE_SPOT_HISTORY_DIALOG, NULL);
}

void spot_history_dialog_set_callsign_and_park(SpotHistoryDialog *self, 
                                                const char *callsign, 
                                                const char *park_ref)
{
  g_return_if_fail(ARTEMIS_IS_SPOT_HISTORY_DIALOG(self));
  
  g_clear_pointer(&self->callsign, g_free);
  g_clear_pointer(&self->park_ref, g_free);
  
  self->callsign = g_strdup(callsign);
  self->park_ref = g_strdup(park_ref);

  // Update dialog title
  g_autofree char *title = g_strdup_printf("%s @ %s", callsign, park_ref);
  adw_window_title_set_title(self->title_widget, title);
}

void spot_history_dialog_show_loading(SpotHistoryDialog *self)
{
  g_return_if_fail(ARTEMIS_IS_SPOT_HISTORY_DIALOG(self));

  gtk_widget_set_visible(GTK_WIDGET(self->loading_page), TRUE);
  gtk_widget_set_visible(GTK_WIDGET(self->history_scroll), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->error_page), FALSE);
}

void spot_history_dialog_show_error(SpotHistoryDialog *self, const char *error_message)
{
  g_return_if_fail(ARTEMIS_IS_SPOT_HISTORY_DIALOG(self));

  if (error_message) {
    adw_status_page_set_description(self->error_page, error_message);
  }

  gtk_widget_set_visible(GTK_WIDGET(self->loading_page), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->history_scroll), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->error_page), TRUE);
}

static GtkWidget *create_spot_row(JsonObject *spot_obj)
{
  // Extract data from JSON
  const char *spotter = json_object_has_member(spot_obj, "spotter") ? 
                        json_object_get_string_member(spot_obj, "spotter") : "";
  const char *frequency = json_object_has_member(spot_obj, "frequency") ? 
                          json_object_get_string_member(spot_obj, "frequency") : "";
  const char *mode = json_object_has_member(spot_obj, "mode") ? 
                     json_object_get_string_member(spot_obj, "mode") : "";
  const char *spot_time = json_object_has_member(spot_obj, "spotTime") ? 
                          json_object_get_string_member(spot_obj, "spotTime") : "";

  g_autoptr(GTimeZone) utc_tz = g_time_zone_new_utc();
  g_autoptr(GDateTime) dt = g_date_time_new_from_iso8601(spot_time, utc_tz);
  g_autofree char *spot_dt = dt ? g_date_time_format(dt, "%x %X UTC") : g_strdup(spot_time);

  const char *comments = json_object_has_member(spot_obj, "comments") ? 
                         json_object_get_string_member(spot_obj, "comments") : "";

  // Create main container
  GtkWidget *row = gtk_list_box_row_new();
  gtk_widget_add_css_class(row, "card");
  gtk_widget_set_margin_top(row, 6);
  gtk_widget_set_margin_bottom(row, 6);
  gtk_widget_set_margin_start(row, 6);
  gtk_widget_set_margin_end(row, 6);

  // Main content box
  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top(main_box, 12);
  gtk_widget_set_margin_bottom(main_box, 12);
  gtk_widget_set_margin_start(main_box, 12);
  gtk_widget_set_margin_end(main_box, 12);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), main_box);

  // Header box with frequency/mode and timestamp
  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_append(GTK_BOX(main_box), header_box);

  // Frequency and mode label
  g_autofree char *freq_mode = g_strdup_printf("%s kHz %s", frequency, mode);
  GtkWidget *freq_label = gtk_label_new(freq_mode);
  gtk_label_set_xalign(GTK_LABEL(freq_label), 0.0);
  gtk_widget_add_css_class(freq_label, "title-4");
  gtk_widget_set_hexpand(freq_label, TRUE);
  gtk_box_append(GTK_BOX(header_box), freq_label);

  // Timestamp label
  GtkWidget *time_label = gtk_label_new(spot_dt);
  gtk_label_set_xalign(GTK_LABEL(time_label), 1.0);
  gtk_widget_add_css_class(time_label, "caption");
  gtk_box_append(GTK_BOX(header_box), time_label);

  // Spotter label
  g_autofree char *spotter_text = g_strdup_printf(_("Spotted by %s"), spotter);
  GtkWidget *spotter_label = gtk_label_new(spotter_text);
  gtk_label_set_xalign(GTK_LABEL(spotter_label), 0.0);
  gtk_widget_add_css_class(spotter_label, "caption");
  gtk_box_append(GTK_BOX(main_box), spotter_label);

  // Comments section if present
  if (comments && *comments && g_strcmp0(comments, "") != 0) {
    GtkWidget *comment_label = gtk_label_new(comments);
    gtk_label_set_xalign(GTK_LABEL(comment_label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(comment_label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(comment_label), PANGO_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(comment_label, "body");
    gtk_widget_set_margin_top(comment_label, 4);
    gtk_box_append(GTK_BOX(main_box), comment_label);
  }

  return row;
}

void spot_history_dialog_show_history(SpotHistoryDialog *self, JsonNode *history_data)
{
  g_return_if_fail(ARTEMIS_IS_SPOT_HISTORY_DIALOG(self));
  g_return_if_fail(history_data != NULL);

  // Clear existing items
  GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(self->history_list));
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_list_box_remove(self->history_list, child);
    child = next;
  }

  if (!JSON_NODE_HOLDS_ARRAY(history_data)) {
    spot_history_dialog_show_error(self, _("Invalid response format from POTA API"));
    return;
  }

  JsonArray *spots_array = json_node_get_array(history_data);
  guint array_length = json_array_get_length(spots_array);

  if (array_length == 0) {
    spot_history_dialog_show_error(self, _("No spot history found"));
    return;
  }

  // Add each spot to the list
  for (guint i = 0; i < array_length; i++) {
    JsonObject *spot_obj = json_array_get_object_element(spots_array, i);
    if (spot_obj) {
      GtkWidget *row = create_spot_row(spot_obj);
      gtk_list_box_append(self->history_list, row);
    }
  }

  // Show the history list
  gtk_widget_set_visible(GTK_WIDGET(self->loading_page), FALSE);
  gtk_widget_set_visible(GTK_WIDGET(self->history_scroll), TRUE);
  gtk_widget_set_visible(GTK_WIDGET(self->error_page), FALSE);
}