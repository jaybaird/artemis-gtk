#include "preferences.h"
#include "artemis.h"
#include "gio/gio.h"
#include "glib-object.h"

#include <math.h>
#include <hamlib/rig.h>

#include "glibconfig.h"
#include "utils.h"
#include "radio_models.h"

typedef struct {
  const char *const *items;
  gsize n_items;
} StringListMap;

/* ---- GSettings mapping helpers ---- */
static gboolean map_str_to_index (GValue *out_prop, GVariant *in_variant, gpointer user_data) {
  StringListMap *m = user_data;
  const char *s = g_variant_get_string (in_variant, NULL);
  guint idx = 0;
  for (guint i = 0; i < m->n_items; i++) {
    if (g_strcmp0 (s, m->items[i]) == 0) { idx = i; break; }
  }
  g_value_set_uint (out_prop, idx);
  return TRUE;
}

static GVariant *map_index_to_str (const GValue *in_prop, const GVariantType *expected, gpointer user_data) {
  StringListMap *m = user_data;
  guint idx = g_value_get_uint (in_prop);
  if (idx >= m->n_items) idx = 0;
  return g_variant_new_string (m->items[idx]);
}

static gboolean map_i_to_d (GValue *out_prop, GVariant *in_variant, gpointer user_data) {
  g_value_set_double (out_prop, (double) g_variant_get_int32 (in_variant));
  return TRUE;
}

static GVariant *map_d_to_i (const GValue *in_prop, const GVariantType *expected, gpointer user_data) {
  int v = (int) llround (g_value_get_double (in_prop));
  return g_variant_new_int32 (v);
}

static const char *const CONNECTION_TYPES_VALUES[] = {
  "none", "serial", "network", "usb"
};

static const char *const BAUD_RATES[] = {
  "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"
};

typedef struct {
  GtkWidget *connection_status_icon;
  GtkWidget *connection_status_label;
  GtkWidget *test_button;
  GtkWidget *parent_dialog;
  GSettings *settings;
} RadioTestData;

typedef struct {
  GtkWidget *serial_settings_group;
  GtkWidget *network_settings_group;
  AdwComboRow *connection_type_row;
} ConnectionTypeData;

static void show_error_dialog(RadioTestData *data, const char *title, const char *message) {
  AdwAlertDialog *alert = ADW_ALERT_DIALOG(adw_alert_dialog_new(title, message));
  adw_alert_dialog_add_response(alert, "ok", "OK");
  adw_alert_dialog_set_default_response(alert, "ok");
  adw_dialog_present(ADW_DIALOG(alert), data->parent_dialog);
}

static void update_connection_status(RadioTestData *data, gboolean connected, const char *message) {
  if (connected) {
    gtk_image_set_from_icon_name(GTK_IMAGE(data->connection_status_icon), "emblem-ok-symbolic");
    gtk_widget_add_css_class(data->connection_status_icon, "success");
    gtk_widget_remove_css_class(data->connection_status_icon, "error");
    gtk_label_set_text(GTK_LABEL(data->connection_status_label), message);
  } else {
    gtk_image_set_from_icon_name(GTK_IMAGE(data->connection_status_icon), "network-offline-symbolic");
    gtk_widget_remove_css_class(data->connection_status_icon, "success");
    gtk_widget_remove_css_class(data->connection_status_icon, "error");
    gtk_label_set_text(GTK_LABEL(data->connection_status_label), "Not tested");
  }
  
  gtk_widget_set_sensitive(data->test_button, TRUE);
  gtk_button_set_label(GTK_BUTTON(data->test_button), "Test Connection");
}

static void on_test_connection_clicked(GtkButton *button, gpointer user_data) {
  RadioTestData *data = (RadioTestData *)user_data;
  
  gtk_widget_set_sensitive(data->test_button, FALSE);
  gtk_button_set_label(GTK_BUTTON(data->test_button), "Testing...");
  
  gtk_image_set_from_icon_name(GTK_IMAGE(data->connection_status_icon), "content-loading-symbolic");
  gtk_widget_remove_css_class(data->connection_status_icon, "success");
  gtk_widget_remove_css_class(data->connection_status_icon, "error");
  gtk_label_set_text(GTK_LABEL(data->connection_status_label), "Testing connection...");
  
  // Get radio settings
  g_autofree gchar *connection_type = g_settings_get_string(data->settings, "radio-connection-type");
  gint model_id = g_settings_get_int(data->settings, "radio-model");
  g_autofree gchar *device_path = g_settings_get_string(data->settings, "radio-device");
  g_autofree gchar *network_host = g_settings_get_string(data->settings, "radio-network-host");
  gint network_port = g_settings_get_int(data->settings, "radio-network-port");
  gint baud_rate = g_settings_get_int(data->settings, "radio-baud-rate");
  
  // Check if connection type is selected
  if (g_strcmp0(connection_type, "none") == 0) {
    show_error_dialog(data, "No Connection Type", "Please select a connection type (Serial, Network, or USB) before testing.");
    update_connection_status(data, FALSE, NULL);
    return;
  }
  
  // Initialize rig
  RIG *rig = rig_init(model_id);
  if (!rig) {
    show_error_dialog(data, "Radio Model Error", "Failed to initialize the selected radio model. Please verify the model selection.");
    update_connection_status(data, FALSE, NULL);
    return;
  }
  
  // Configure connection based on type
  if (g_strcmp0(connection_type, "serial") == 0) {
    strncpy(rig->state.rigport.pathname, device_path, HAMLIB_FILPATHLEN - 1);
    rig->state.rigport.parm.serial.rate = baud_rate;
  } else if (g_strcmp0(connection_type, "network") == 0) {
    g_autofree gchar *network_path = g_strdup_printf("%s:%d", network_host, network_port);
    strncpy(rig->state.rigport.pathname, network_path, HAMLIB_FILPATHLEN - 1);
    rig->state.rigport.type.rig = RIG_PORT_NETWORK;
  } else if (g_strcmp0(connection_type, "usb") == 0) {
    strncpy(rig->state.rigport.pathname, device_path, HAMLIB_FILPATHLEN - 1);
    rig->state.rigport.type.rig = RIG_PORT_USB;
  }
  
  // Test connection
  int result = rig_open(rig);
  if (result == RIG_OK) {
    // Try to get frequency to verify communication
    freq_t freq;
    int freq_result = rig_get_freq(rig, RIG_VFO_CURR, &freq);
    rig_close(rig);
    
    if (freq_result == RIG_OK) {
      g_autofree gchar *msg = g_strdup_printf("Connected (%.3f MHz)", freq / 1000000.0);
      update_connection_status(data, TRUE, msg);
    } else {
      update_connection_status(data, TRUE, "Connected");
    }
  } else {
    g_autofree gchar *error_detail = g_strdup_printf("Connection failed: %s\n\nPlease verify your connection settings and ensure your radio is powered on and properly connected.", rigerror(result));
    show_error_dialog(data, "Radio Connection Failed", error_detail);
    update_connection_status(data, FALSE, NULL);
  }
  
  rig_cleanup(rig);
}

/* Baud rate mapping functions */
static gboolean baud_rate_to_index(GValue *out_prop, GVariant *in_variant, gpointer user_data) {
  gint baud = g_variant_get_int32(in_variant);
  g_autofree gchar *baud_str = g_strdup_printf("%d", baud);
  guint idx = 0;
  for (guint i = 0; i < G_N_ELEMENTS(BAUD_RATES); i++) {
    if (g_strcmp0(baud_str, BAUD_RATES[i]) == 0) { idx = i; break; }
  }
  g_value_set_uint(out_prop, idx);
  return TRUE;
}

static GVariant *index_to_baud_rate(const GValue *in_prop, const GVariantType *expected, gpointer user_data) {
  guint idx = g_value_get_uint(in_prop);
  if (idx >= G_N_ELEMENTS(BAUD_RATES)) idx = 3; // default to 9600
  gint baud = g_ascii_strtoll(BAUD_RATES[idx], NULL, 10);
  return g_variant_new_int32(baud);
}

/* Connection type mapping functions */
static gboolean connection_type_to_index(GValue *out_prop, GVariant *in_variant, gpointer user_data) {
  const char *value = g_variant_get_string(in_variant, NULL);
  guint idx = 0;
  for (guint i = 0; i < G_N_ELEMENTS(CONNECTION_TYPES_VALUES); i++) {
    if (g_strcmp0(value, CONNECTION_TYPES_VALUES[i]) == 0) { idx = i; break; }
  }
  g_value_set_uint(out_prop, idx);
  return TRUE;
}

static GVariant *index_to_connection_type(const GValue *in_prop, const GVariantType *expected, gpointer user_data) {
  guint idx = g_value_get_uint(in_prop);
  if (idx >= G_N_ELEMENTS(CONNECTION_TYPES_VALUES)) idx = 0; // default to "none"
  return g_variant_new_string(CONNECTION_TYPES_VALUES[idx]);
}

/* Radio model mapping functions */
static gboolean radio_model_to_index(GValue *out_prop, GVariant *in_variant, gpointer user_data) {
  gint model_id = g_variant_get_int32(in_variant);
  guint idx = 0;
  for (guint i = 0; i < RADIO_MODELS_COUNT; i++) {
    if (RADIO_MODELS[i].model_id == model_id) { idx = i; break; }
  }
  g_value_set_uint(out_prop, idx);
  return TRUE;
}

static GVariant *index_to_radio_model(const GValue *in_prop, const GVariantType *expected, gpointer user_data) {
  guint idx = g_value_get_uint(in_prop);
  if (idx >= RADIO_MODELS_COUNT) idx = 0; // default to dummy
  return g_variant_new_int32(RADIO_MODELS[idx].model_id);
}

static void radio_test_data_free(RadioTestData *data) {
  if (data) {
    g_free(data);
  }
}

static void connection_type_data_free(ConnectionTypeData *data) {
  if (data) {
    g_free(data);
  }
}

static void on_connection_type_changed(AdwComboRow *row, GParamSpec *pspec, gpointer user_data) {
  ConnectionTypeData *data = (ConnectionTypeData *)user_data;
  guint selected = adw_combo_row_get_selected(row);
  
  // Get the connection type string from the values array (lowercase)
  const char *connection_type = (selected < G_N_ELEMENTS(CONNECTION_TYPES_VALUES)) ? 
                                CONNECTION_TYPES_VALUES[selected] : "none";
  
  // Show/hide groups based on connection type
  gboolean show_serial = (g_strcmp0(connection_type, "serial") == 0 || 
                         g_strcmp0(connection_type, "usb") == 0);
  gboolean show_network = (g_strcmp0(connection_type, "network") == 0);
  
  gtk_widget_set_visible(data->serial_settings_group, show_serial);
  gtk_widget_set_visible(data->network_settings_group, show_network);
}

gboolean spot_preferences_is_configured()
{
  GSettings *settings = artemis_app_get_settings();
  g_autofree gchar *callsign = g_settings_get_string (settings, "callsign");
  gboolean is_set = g_settings_get_user_value(settings, "callsign") != NULL;
  return is_set && (callsign && *callsign);
}

void show_preferences_dialog(GtkWidget *parent)
{
  GSettings *settings = artemis_app_get_settings();
  g_autoptr(GtkBuilder) b = gtk_builder_new_from_resource("/com/k0vcz/artemis/data/ui/preferences.ui");

  GtkWidget  *dlg   = GTK_WIDGET(gtk_builder_get_object (b, "prefs_dialog"));
  AdwSpinRow *row_interval   = ADW_SPIN_ROW (gtk_builder_get_object(b, "row_update_interval"));
  AdwComboRow *row_band      = ADW_COMBO_ROW(gtk_builder_get_object(b, "row_default_band"));
  AdwComboRow *row_mode      = ADW_COMBO_ROW(gtk_builder_get_object(b, "row_default_mode"));
  AdwEntryRow *row_callsign  = ADW_ENTRY_ROW(gtk_builder_get_object(b, "row_callsign"));
  AdwEntryRow *row_location  = ADW_ENTRY_ROW(gtk_builder_get_object(b, "row_location"));
  AdwEntryRow *row_spot_msg  = ADW_ENTRY_ROW(gtk_builder_get_object(b, "row_spot_message"));
  AdwEntryRow *row_qrz_key   = ADW_ENTRY_ROW(gtk_builder_get_object(b, "row_qrz_api_key"));

  // Radio settings widgets
  AdwComboRow *row_connection_type = ADW_COMBO_ROW(gtk_builder_get_object(b, "row_connection_type"));
  AdwComboRow *row_radio_model     = ADW_COMBO_ROW(gtk_builder_get_object(b, "row_radio_model"));
  AdwEntryRow *row_device_path     = ADW_ENTRY_ROW(gtk_builder_get_object(b, "row_device_path"));
  AdwComboRow *row_baud_rate       = ADW_COMBO_ROW(gtk_builder_get_object(b, "row_baud_rate"));
  AdwEntryRow *row_network_host    = ADW_ENTRY_ROW(gtk_builder_get_object(b, "row_network_host"));
  AdwSpinRow  *row_network_port    = ADW_SPIN_ROW(gtk_builder_get_object(b, "row_network_port"));
  
  // Test connection widgets
  GtkWidget *connection_status_icon  = GTK_WIDGET(gtk_builder_get_object(b, "connection_status_icon"));
  GtkWidget *connection_status_label = GTK_WIDGET(gtk_builder_get_object(b, "connection_status_label"));
  GtkWidget *test_connection_button  = GTK_WIDGET(gtk_builder_get_object(b, "test_connection_button"));
  
  // Settings groups for show/hide
  GtkWidget *serial_settings_group   = GTK_WIDGET(gtk_builder_get_object(b, "serial_settings_group"));
  GtkWidget *network_settings_group  = GTK_WIDGET(gtk_builder_get_object(b, "network_settings_group"));

  /* Ensure combo rows display strings from GtkStringList */
  {
    GtkExpression *expr = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    adw_combo_row_set_expression(row_band, expr);
    gtk_expression_unref(expr);
  }
  {
    GtkExpression *expr = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    adw_combo_row_set_expression(row_mode, expr);
    gtk_expression_unref(expr);
  }
  
  /* Radio combo row expressions */
  {
    GtkExpression *expr = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    adw_combo_row_set_expression(row_connection_type, expr);
    gtk_expression_unref(expr);
  }
  {
    GtkExpression *expr = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    adw_combo_row_set_expression(row_baud_rate, expr);
    gtk_expression_unref(expr);
  }
  
  /* Populate radio models list and set expression */
  {
    GtkStringList *radio_models_list = GTK_STRING_LIST(gtk_builder_get_object(b, "radio_models_model"));
    
    /* Clear the default dummy entry and populate with all models */
    gtk_string_list_splice(radio_models_list, 0, 1, NULL);
    for (guint i = 0; i < RADIO_MODELS_COUNT; i++) {
      gtk_string_list_append(radio_models_list, RADIO_MODELS[i].display_name);
    }
    
    GtkExpression *expr = gtk_property_expression_new (GTK_TYPE_STRING_OBJECT, NULL, "string");
    adw_combo_row_set_expression(row_radio_model, expr);
    gtk_expression_unref(expr);
  }

  /* Spin range to match your schema's <range> */
  adw_spin_row_set_range(row_interval, 60.0, 3600.0);
  adw_spin_row_set_range(row_network_port, 1.0, 65535.0);

  g_settings_bind(settings, "callsign",      row_callsign, "text", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(settings, "location",      row_location, "text", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(settings, "spot-message",  row_spot_msg, "text", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(settings, "qrz-api-key",   row_qrz_key,  "text", G_SETTINGS_BIND_DEFAULT);

  /* Radio settings bindings */
  g_settings_bind(settings, "radio-device",       row_device_path,  "text", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(settings, "radio-network-host", row_network_host, "text", G_SETTINGS_BIND_DEFAULT);
  
  g_settings_bind_with_mapping(settings, "radio-model",
                                row_radio_model, "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                radio_model_to_index, index_to_radio_model, NULL, NULL);
                                
  g_settings_bind_with_mapping(settings, "radio-network-port",
                                row_network_port, "value",
                                G_SETTINGS_BIND_DEFAULT,
                                map_i_to_d, map_d_to_i, NULL, NULL);

  /* int <-> double mapping for the SpinRow */
  g_settings_bind_with_mapping(settings, "update-interval",
                                row_interval, "value",
                                G_SETTINGS_BIND_DEFAULT,
                                map_i_to_d, map_d_to_i, NULL, NULL);

  /* string <-> index mapping for combo rows */
  StringListMap bands_map = { .items = BANDS, .n_items = G_N_ELEMENTS(BANDS) };
  g_settings_bind_with_mapping(settings, "default-band",
                                row_band, "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                map_str_to_index, map_index_to_str,
                                &bands_map, NULL);

  StringListMap modes_map = { .items = MODES, .n_items = G_N_ELEMENTS(MODES) };
  g_settings_bind_with_mapping(settings, "default-mode",
                                row_mode, "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                map_str_to_index, map_index_to_str,
                                &modes_map, NULL);

  /* Radio combo row bindings */
  g_settings_bind_with_mapping(settings, "radio-connection-type",
                                row_connection_type, "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                connection_type_to_index, index_to_connection_type,
                                NULL, NULL);

  /* Baud rate binding */
  g_settings_bind_with_mapping(settings, "radio-baud-rate",
                                row_baud_rate, "selected",
                                G_SETTINGS_BIND_DEFAULT,
                                baud_rate_to_index, index_to_baud_rate,
                                NULL, NULL);

  /* Setup test connection button */
  RadioTestData *test_data = g_new0(RadioTestData, 1);
  test_data->connection_status_icon = connection_status_icon;
  test_data->connection_status_label = connection_status_label;
  test_data->test_button = test_connection_button;
  test_data->parent_dialog = dlg;
  test_data->settings = settings;
  
  g_signal_connect(test_connection_button, "clicked", G_CALLBACK(on_test_connection_clicked), test_data);
  g_object_set_data_full(G_OBJECT(dlg), "radio-test-data", test_data, (GDestroyNotify)radio_test_data_free);

  /* Setup connection type change handler */
  ConnectionTypeData *connection_data = g_new0(ConnectionTypeData, 1);
  connection_data->serial_settings_group = serial_settings_group;
  connection_data->network_settings_group = network_settings_group;
  connection_data->connection_type_row = row_connection_type;
  
  g_signal_connect(row_connection_type, "notify::selected", G_CALLBACK(on_connection_type_changed), connection_data);
  g_object_set_data_full(G_OBJECT(dlg), "connection-type-data", connection_data, (GDestroyNotify)connection_type_data_free);
  
  /* Set initial visibility based on current connection type */
  on_connection_type_changed(row_connection_type, NULL, connection_data);

  /* Present */
  adw_dialog_present(ADW_DIALOG (dlg), GTK_WIDGET(parent));
}