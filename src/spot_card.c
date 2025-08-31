#include "spot_card.h"

#include "adwaita.h"
#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "gtk/gtk.h"
#include "gtk/gtkshortcut.h"
#include "preferences.h"
#include "spot.h"
#include "spot_page.h"
#include "utils.h"
#include "database.h"
#include "spot_repo.h"
#include "pota_user_cache.h"
#include "artemis.h"
#include "spot_history_dialog.h"

#include <glib/gi18n.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

struct _SpotCard {
  GtkWidget parent_instance;

  AdwAvatar *activator_avatar;
  GtkLabel *title;
  AdwAvatar *hunter_avatar;
  GtkLabel *hunter_callsign;
  GtkLabel *park;
  GtkLabel *frequency;
  GtkLabel *mode;
  GtkLabel *spots;
  GtkLabel *time;
  GtkLabel *location_desc;
  GtkLabel *park_label;
  GtkImage *corner_image;

  GtkButton *spot_button;
  GtkButton *tune_button;
  GtkButton *history_button;
  GtkButton *park_details_button;

  SpotHistoryDialog *history_dialog;
  GWeakRef spot;
};

G_DEFINE_FINAL_TYPE(SpotCard, spot_card, GTK_TYPE_BOX);

static void spot_card_dispose(GObject *gobject)
{
  SpotCard *self = ARTEMIS_SPOT_CARD(gobject);
  g_weak_ref_clear(&self->spot);
  g_clear_object(&self->history_dialog);

  gtk_widget_dispose_template(GTK_WIDGET(gobject), ARTEMIS_TYPE_SPOT_CARD);
  G_OBJECT_CLASS(spot_card_parent_class)->dispose(gobject);
}

static void on_alert_response (AdwAlertDialog *dialog,
                               const char     *response,
                               gpointer        user_data)
{
    if (g_strcmp0 (response, "prefs") == 0) {
        show_preferences_dialog (GTK_WIDGET(dialog));
    }
}

static void on_spot_button_clicked(GtkButton *button, gpointer user_data)
{
  SpotCard *self = ARTEMIS_SPOT_CARD(user_data);
  ArtemisSpot *spot = g_weak_ref_get(&self->spot);

  if (spot_preferences_is_configured())
  {
    show_spot_page_with_spot(GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button))), spot);
  }
  else 
  {
    AdwDialog *alert = adw_alert_dialog_new (
        _("Configuration required"),
        _("You must set your callsign in Preferences before posting a spot.")
    );

    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(alert),
                                  "cancel",   _("_Cancel"),
                                  "prefs",    _("_Open Preferences"),
                                  NULL);

    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(alert), "prefs");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(alert), "cancel");
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(alert), "prefs", ADW_RESPONSE_SUGGESTED);
    adw_dialog_present(alert, GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(button))));

    g_signal_connect(alert, "response", G_CALLBACK(on_alert_response), NULL);
  }
  g_object_unref(spot);
}

static void on_tune_button_clicked(GtkButton *button, gpointer user_data)
{
  SpotCard *self = ARTEMIS_SPOT_CARD(user_data);
  ArtemisSpot *spot = g_weak_ref_get(&self->spot);
  
  if (!spot) {
    return;
  }
  
  // Get the default app instance and emit the tune signal
  ArtemisApp *app = ARTEMIS_APP(g_application_get_default());
  
  double frequency_hz = artemis_spot_get_frequency_hz(spot);
  artemis_app_emit_tune_frequency(app, frequency_hz, spot);
  
  g_object_unref(spot);
}

static void on_spot_history_received(GObject *source, GAsyncResult *result, gpointer user_data)
{
  PotaClient *client = ARTEMIS_POTA_CLIENT(source);
  SpotCard *self = ARTEMIS_SPOT_CARD(user_data);
  
  GError *error = NULL;
  JsonNode *root = pota_client_get_spot_history_finish(client, result, &error);
  
  if (error) {
    g_warning("Failed to fetch spot history: %s", error->message);
    if (self->history_dialog) {
      spot_history_dialog_show_error(self->history_dialog, error->message);
    }
    g_error_free(error);
    return;
  }
  
  if (root && self->history_dialog) {
    spot_history_dialog_show_history(self->history_dialog, root);
    json_node_unref(root);
  }
}

static void on_history_button_clicked(GtkButton *button, gpointer user_data)
{
  SpotCard *self = ARTEMIS_SPOT_CARD(user_data);
  ArtemisSpot *spot = g_weak_ref_get(&self->spot);
  
  if (!spot) {
    g_debug("Spot is NULL in on_history_button_clicked");
    return;
  }
  
  const char *callsign = artemis_spot_get_callsign(spot);
  const char *park_ref = artemis_spot_get_park_ref(spot);
  
  g_debug("Retrieved from spot: callsign='%s', park_ref='%s'", callsign ? callsign : "NULL", park_ref ? park_ref : "NULL");
  
  if (!callsign || !park_ref) {
    g_warning("Missing callsign or park_ref for history request");
    g_object_unref(spot);
    return;
  }
  
  // Create dialog if it doesn't exist
  if (!self->history_dialog) {
    self->history_dialog = spot_history_dialog_new();
  }
  
  // Set dialog title and show loading state
  spot_history_dialog_set_callsign_and_park(self->history_dialog, callsign, park_ref);
  spot_history_dialog_show_loading(self->history_dialog);
  
  // Present the dialog
  GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));
  if (root && GTK_IS_WINDOW(root)) {
    adw_dialog_present(ADW_DIALOG(self->history_dialog), GTK_WIDGET(root));
  }
  
  // Get the POTA client from the app's spot repo and fetch data
  ArtemisApp *app = ARTEMIS_APP(g_application_get_default());
  if (app) {
    ArtemisSpotRepo *repo = artemis_app_get_spot_repo(app);
    if (repo) {
      PotaClient *client = artemis_spot_repo_get_pota_client(repo);
      if (client) {
        g_debug("Fetching spot history for %s @ %s", callsign, park_ref);
        g_debug("Callsign length: %zu, Park ref length: %zu", strlen(callsign), strlen(park_ref));
        pota_client_get_spot_history_async(client, callsign, park_ref, NULL, 
                                           on_spot_history_received, self);
      }
    }
  }
  
  g_object_unref(spot);
}

static void on_park_details_button_clicked(GtkButton *button, gpointer user_data)
{
  SpotCard *self = ARTEMIS_SPOT_CARD(user_data);
  ArtemisSpot *spot = g_weak_ref_get(&self->spot);
  
  if (!spot) {
    g_debug("Spot is NULL in on_park_details_button_clicked");
    return;
  }
  
  const char *park_ref = artemis_spot_get_park_ref(spot);
  
  if (!park_ref || !*park_ref) {
    g_warning("Missing park_ref for park details request");
    g_object_unref(spot);
    return;
  }
  
  // Construct the POTA park page URL
  g_autofree char *url = g_strdup_printf("https://pota.app/#/park/%s", park_ref);
  
  g_debug("Opening park details URL: %s", url);
  
  // Open URL in default browser using GTK4 API
  GtkUriLauncher *launcher = gtk_uri_launcher_new(url);
  
  // Get the parent window for the launcher
  GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(self));
  GtkWindow *parent_window = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : NULL;
  
  gtk_uri_launcher_launch(launcher, parent_window, NULL, NULL, NULL);
  g_object_unref(launcher);
  
  g_object_unref(spot);
}

typedef struct {
  SpotCard *card;
  AdwAvatar *target_avatar;
  gchar *callsign; // for gravatar fallback
} AvatarUpdateData;

static void avatar_update_data_free(AvatarUpdateData *data) {
  if (data) {
    g_object_unref(data->card);
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

static void fetch_gravatar_async(const char *gravatar_hash, AvatarUpdateData *data) {
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

static void on_avatar_data_fetched(GObject *source, GAsyncResult *result, gpointer user_data) {
  ArtemisPotaUserCache *cache = ARTEMIS_POTA_USER_CACHE(source);
  AvatarUpdateData *data = (AvatarUpdateData *)user_data;
  
  GError *error = NULL;
  ArtemisActivator *activator = artemis_pota_user_cache_get_finish(cache, result, &error);
  
  if (activator) {
    const char *name = artemis_activator_get_name(activator);
    if (name && *name) {
      adw_avatar_set_text(data->target_avatar, name);
    }
    
    // Try to load Gravatar using the hash from POTA API
    const char *gravatar_hash = artemis_activator_get_gravatar_hash(activator);
    if (gravatar_hash && *gravatar_hash) {
      // Create new data for Gravatar callback (don't free the current one yet)
      AvatarUpdateData *gravatar_data = g_new0(AvatarUpdateData, 1);
      gravatar_data->card = g_object_ref(data->card);
      gravatar_data->target_avatar = data->target_avatar;
      gravatar_data->callsign = g_strdup(data->callsign);
      
      fetch_gravatar_async(gravatar_hash, gravatar_data);
    }
    
    g_object_unref(activator);
  } else if (error) {
    g_debug("Failed to fetch avatar data: %s", error->message);
    g_clear_error(&error);
    
    // Fallback to text avatar with callsign
    if (data->callsign && *data->callsign) {
      adw_avatar_set_text(data->target_avatar, data->callsign);
    }
  }
  
  avatar_update_data_free(data);
}

static void spot_card_class_init(SpotCardClass *klass) {
  g_type_ensure(ADW_TYPE_CLAMP);
  g_type_ensure(ADW_TYPE_AVATAR);

  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource(widget_class, "/com/k0vcz/artemis/data/ui/spot_card.ui");
  gtk_widget_class_bind_template_callback (widget_class, on_spot_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_tune_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_history_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_park_details_button_clicked);

  gtk_widget_class_bind_template_child(widget_class, SpotCard, activator_avatar);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, title);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, hunter_avatar);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, hunter_callsign);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, frequency);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, mode);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, spots);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, time);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, location_desc);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, park_label);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, corner_image);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, spot_button);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, tune_button);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, history_button);
  gtk_widget_class_bind_template_child(widget_class, SpotCard, park_details_button);

  gtk_widget_class_bind_template_callback(widget_class, G_CALLBACK(on_spot_button_clicked));

  G_OBJECT_CLASS(klass)->dispose = spot_card_dispose;
}

static void spot_card_init(SpotCard *self) {
  gtk_widget_init_template(GTK_WIDGET(self));
}

SpotCard *spot_card_new(void) {
  return g_object_new(ARTEMIS_TYPE_SPOT_CARD, NULL);
}

GtkWidget *spot_card_new_from_spot(gpointer user_data)
{
  ArtemisSpot *spot = ARTEMIS_SPOT(user_data);
  SpotCard *card = spot_card_new();

  const char *callsign = artemis_spot_get_callsign(spot);
  const char *park_ref = artemis_spot_get_park_ref(spot);
  const char *park_name = artemis_spot_get_park_name(spot);

  g_autofree const char *title = format_title(callsign, park_ref);
  g_autofree const char *park_uri = park_uri_from_ref(park_ref);

  g_autofree char *freq = g_strdup_printf("%d kHz", artemis_spot_get_frequency_hz(spot));
  g_autofree char *spot_count = g_strdup_printf("%d", artemis_spot_get_spot_count(spot));

  gtk_label_set_label(card->title, title);
  gtk_label_set_label(card->park_label, park_name);
  gtk_label_set_label(card->location_desc, artemis_spot_get_location_desc(spot));
  gtk_label_set_label(card->frequency, freq);
  gtk_label_set_label(card->mode, artemis_spot_get_mode(spot));
  gtk_label_set_label(card->hunter_callsign, artemis_spot_get_spotter(spot));
  gtk_label_set_label(card->spots, spot_count);
  gtk_label_set_label(card->time, humanize_ago(artemis_spot_get_spot_time(spot)));

  g_weak_ref_init(&card->spot, spot);

  // Check if we've hunted this park today and show/hide corner image
  SpotDb *db = spot_db_get_instance();
  g_autoptr(GDateTime) now = g_date_time_new_now_utc();
  GError *db_err = NULL;
  gboolean hunted_today = spot_db_had_qso_with_park_on_utc_day(db, park_ref, now, &db_err);
  
  if (db_err) {
    g_debug("Error checking if park %s was hunted today: %s", park_ref, db_err->message);
    g_clear_error(&db_err);
    hunted_today = FALSE; // Default to not hunted if error
  }
  
  spot_card_set_corner_image_visible(card, hunted_today);

  if (hunted_today)
  {
    gtk_widget_add_css_class(GTK_WIDGET(card), "dimmed");
  }

  // Check if activator is QRT and apply dimmed styling
  const char *activator_comment = g_utf8_strup(artemis_spot_get_activator_comment(spot), -1);
  if (activator_comment && g_strstr_len(activator_comment, -1, "QRT")) {
    gtk_widget_add_css_class(GTK_WIDGET(card), "dimmed");
  }

  // Fetch activator avatar asynchronously
  ArtemisPotaUserCache *cache = artemis_pota_user_cache_get_instance();
  if (cache && callsign && *callsign) {
    AvatarUpdateData *activator_data = g_new0(AvatarUpdateData, 1);
    activator_data->card = g_object_ref(card);
    activator_data->target_avatar = card->activator_avatar;
    activator_data->callsign = g_strdup(callsign);
    
    artemis_pota_user_cache_get_async(cache, callsign, 3600, NULL,
                                      on_avatar_data_fetched, activator_data);
  }

  // Fetch spotter/hunter avatar asynchronously
  const char *spotter = artemis_spot_get_spotter(spot);
  if (cache && spotter && *spotter) {
    AvatarUpdateData *spotter_data = g_new0(AvatarUpdateData, 1);
    spotter_data->card = g_object_ref(card);
    spotter_data->target_avatar = card->hunter_avatar;
    spotter_data->callsign = g_strdup(spotter);
    
    artemis_pota_user_cache_get_async(cache, spotter, 3600, NULL,
                                      on_avatar_data_fetched, spotter_data);
  }

  return GTK_WIDGET(card);
}

void spot_card_set_corner_image_visible(SpotCard *self, gboolean visible)
{
  g_return_if_fail(ARTEMIS_IS_SPOT_CARD(self));
  gtk_widget_set_visible(GTK_WIDGET(self->corner_image), visible);
}

void spot_card_update_pinned_state(SpotCard *self)
{
  g_return_if_fail(ARTEMIS_IS_SPOT_CARD(self));
  
  ArtemisSpot *card_spot = g_weak_ref_get(&self->spot);
  if (!card_spot) {
    return;
  }
  
  // Get the pinned spot from the app
  ArtemisApp *app = ARTEMIS_APP(g_application_get_default());
  ArtemisSpot *pinned_spot = artemis_app_get_pinned_spot(app);
  
  // Check if this card's spot is the pinned spot
  gboolean is_pinned = (pinned_spot && card_spot == pinned_spot);
  
  // Add or remove the pinned CSS class to the inner card box
  // The SpotCard is a GtkBox that contains an AdwClamp, which contains another Box with "card" and "frame" styles
  GtkWidget *clamp = gtk_widget_get_first_child(GTK_WIDGET(self));
  if (clamp && ADW_IS_CLAMP(clamp)) {
    GtkWidget *inner_box = gtk_widget_get_first_child(clamp);
    if (inner_box && GTK_IS_BOX(inner_box)) {
      if (is_pinned) {
        g_debug("Adding 'pinned' CSS class to spot card for %s", artemis_spot_get_callsign(card_spot));
        gtk_widget_add_css_class(inner_box, "pinned");
      } else {
        g_debug("Removing 'pinned' CSS class from spot card for %s", artemis_spot_get_callsign(card_spot));
        gtk_widget_remove_css_class(inner_box, "pinned");
      }
    }
  }
  
  // Cleanup
  g_object_unref(card_spot);
  if (pinned_spot) {
    g_object_unref(pinned_spot);
  }
}
