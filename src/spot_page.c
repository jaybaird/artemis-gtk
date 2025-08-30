#include "spot_page.h"
#include "adwaita.h"

#include "gio/gio.h"
#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "gtk/gtk.h"

#include "artemis.h"
#include "gtk/gtkshortcut.h"
#include "spot.h"

typedef struct {
  GtkBuilder  *builder;
  GWeakRef    spot;
} SpotPageContext;

static void
spot_page_ctx_free(SpotPageContext *ctx)
{
    if (!ctx) return;
    g_clear_object(&ctx->builder);
    g_weak_ref_clear(&ctx->spot);
    g_free(ctx);
}

static void on_spot_page_cancel(GtkButton *button, gpointer user_data)
{
  GtkWidget *dialog = GTK_WIDGET(user_data);
  if (dialog)
  {
    adw_dialog_close(ADW_DIALOG(dialog));
  }
}

static void on_spot_page_submit(GtkButton *button, gpointer user_data)
{
  GtkWidget *dialog = GTK_WIDGET(user_data);
  if (!dialog) return;

  SpotPageContext *ctx = g_object_get_data(G_OBJECT(dialog), "spot-page-ctx");

  AdwEntryRow *activator_callsign_row   = ADW_ENTRY_ROW(gtk_builder_get_object(ctx->builder, "activator_callsign"));
  AdwEntryRow *spotter_callsign_row     = ADW_ENTRY_ROW(gtk_builder_get_object(ctx->builder, "spotter_callsign"));
  AdwEntryRow *frequency_row            = ADW_ENTRY_ROW(gtk_builder_get_object(ctx->builder, "frequency"));
  AdwEntryRow *park_ref_row             = ADW_ENTRY_ROW(gtk_builder_get_object(ctx->builder, "park_ref"));
  AdwEntryRow *spotter_comments_row     = ADW_ENTRY_ROW(gtk_builder_get_object(ctx->builder, "spotter_comments"));

  AdwComboRow *mode_row                 = ADW_COMBO_ROW(gtk_builder_get_object(ctx->builder, "mode"));

  const char *activator_str   = gtk_editable_get_text(GTK_EDITABLE(activator_callsign_row));
  const char *spotter_str     = gtk_editable_get_text(GTK_EDITABLE(spotter_callsign_row));
  const char *freq_str        = gtk_editable_get_text(GTK_EDITABLE(frequency_row));
  const char *park            = gtk_editable_get_text(GTK_EDITABLE(park_ref_row));
  const char *comment         = gtk_editable_get_text(GTK_EDITABLE(spotter_comments_row));
  
  const char *mode = NULL;
  if (mode_row)
  {
    guint idx = adw_combo_row_get_selected(mode_row);
    GtkStringList *model = GTK_STRING_LIST(adw_combo_row_get_model(mode_row));
    if (idx < g_list_model_get_n_items(G_LIST_MODEL(model)))
    {
      mode = gtk_string_list_get_string(model, idx);
    }
  }
  else 
  {
    ArtemisSpot *old_spot = g_weak_ref_get(&ctx->spot);
    mode = artemis_spot_get_mode(old_spot);
    g_object_unref(old_spot);
  }

  int freq_hz = (int)g_ascii_strtoll(freq_str, NULL, 10);
  g_autoptr(GDateTime) dt = g_date_time_new_now_utc();
  ArtemisSpot *spot = artemis_spot_new(
      activator_str,          
      park,              
      NULL,              
      NULL,              
      NULL,              
      freq_hz,           
      mode ? mode : "",              
      dt,              
      spotter_str,              
      comment,           
      0                  
  );

  ArtemisApp *app = ARTEMIS_APP(g_application_get_default());
  artemis_app_emit_spot_submitted(app, spot);

  g_object_unref(spot);

  adw_dialog_close(ADW_DIALOG(dialog));
}

void show_add_spot_page(GtkWidget *parent)
{
  GSettings *settings = artemis_app_get_settings();
  GtkBuilder *b = gtk_builder_new_from_resource("/com/k0vcz/artemis/data/ui/add_spot_page.ui");

  GtkWidget  *dlg   = GTK_WIDGET(gtk_builder_get_object (b, "spot_page"));

  AdwEntryRow *spotter_callsign_row     = ADW_ENTRY_ROW(gtk_builder_get_object(b, "spotter_callsign"));
  AdwEntryRow *spotter_comments_row     = ADW_ENTRY_ROW(gtk_builder_get_object(b, "spotter_comments"));

  GtkButton *submit_button              = GTK_BUTTON(gtk_builder_get_object(b, "submit_button"));
  GtkButton *cancel_button              = GTK_BUTTON(gtk_builder_get_object(b, "cancel_button"));

  g_autofree gchar *spotter_callsign  = g_settings_get_string (settings, "callsign");
  g_autofree gchar *default_msg       = g_settings_get_string (settings, "spot-message");
  
  gtk_editable_set_text(GTK_EDITABLE(spotter_callsign_row), spotter_callsign);
  gtk_editable_set_text(GTK_EDITABLE(spotter_comments_row), default_msg);

  SpotPageContext *ctx = g_new0(SpotPageContext, 1);
  ctx->builder = g_object_ref(b);

  g_object_set_data_full(G_OBJECT(dlg), "spot-page-ctx", ctx, (GDestroyNotify)spot_page_ctx_free);

  g_signal_connect(submit_button, "clicked", G_CALLBACK(on_spot_page_submit), dlg);
  g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_spot_page_cancel), dlg);

  adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(parent));
}

void show_spot_page_with_spot(GtkWidget *parent, ArtemisSpot *spot)
{
  GSettings *settings = artemis_app_get_settings();
  GtkBuilder *b = gtk_builder_new_from_resource("/com/k0vcz/artemis/data/ui/spot_page.ui");

  GtkWidget  *dlg   = GTK_WIDGET(gtk_builder_get_object (b, "spot_page"));

  AdwEntryRow *activator_callsign_row   = ADW_ENTRY_ROW(gtk_builder_get_object(b, "activator_callsign"));
  AdwEntryRow *spotter_callsign_row     = ADW_ENTRY_ROW(gtk_builder_get_object(b, "spotter_callsign"));
  AdwEntryRow *frequency_row            = ADW_ENTRY_ROW(gtk_builder_get_object(b, "frequency"));
  AdwEntryRow *park_ref_row             = ADW_ENTRY_ROW(gtk_builder_get_object(b, "park_ref"));
  AdwEntryRow *spotter_comments_row     = ADW_ENTRY_ROW(gtk_builder_get_object(b, "spotter_comments"));

  GtkButton *submit_button              = GTK_BUTTON(gtk_builder_get_object(b, "submit_button"));
  GtkButton *cancel_button              = GTK_BUTTON(gtk_builder_get_object(b, "cancel_button"));

  g_autofree gchar *spotter_callsign  = g_settings_get_string (settings, "callsign");
  g_autofree gchar *default_msg       = g_settings_get_string (settings, "spot-message");
  g_autofree gchar *frequency_str     = g_strdup_printf("%d", artemis_spot_get_frequency_hz(spot));
  gtk_editable_set_text(GTK_EDITABLE(activator_callsign_row), artemis_spot_get_callsign(spot));
  gtk_editable_set_text(GTK_EDITABLE(spotter_callsign_row), spotter_callsign);
  gtk_editable_set_text(GTK_EDITABLE(frequency_row), frequency_str);
  gtk_editable_set_text(GTK_EDITABLE(park_ref_row), artemis_spot_get_park_ref(spot));
  gtk_editable_set_text(GTK_EDITABLE(spotter_comments_row), default_msg);

  SpotPageContext *ctx = g_new0(SpotPageContext, 1);
  ctx->builder = g_object_ref(b);
  g_weak_ref_init(&ctx->spot, spot);

  g_object_set_data_full(G_OBJECT(dlg), "spot-page-ctx", ctx, (GDestroyNotify)spot_page_ctx_free);

  g_signal_connect(submit_button, "clicked", G_CALLBACK(on_spot_page_submit), dlg);
  g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_spot_page_cancel), dlg);

  adw_dialog_present(ADW_DIALOG (dlg), GTK_WIDGET(parent));
  g_object_unref(b);
}