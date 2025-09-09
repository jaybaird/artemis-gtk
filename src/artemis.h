#pragma once

#include <adwaita.h>

#include "spot.h"

#define APPLICATION_ID "com.k0vcz.artemis"
#define RESOURCE_PATH "/com/k0vcz/artemis/data/"

#define VERSION_MAJOR(version) ((uint32_t)(version) >> 22)
#define VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3ff)
#define VERSION_PATCH(version) ((uint32_t)(version) & 0xfff)	

#define MAKE_VERSION(major, minor, patch) \
    (((major) << 22) | ((minor) << 12) | (patch))

static uint32_t APP_VERSION = MAKE_VERSION(1, 0, 0);

#include <glib/gi18n.h>

enum {
  SIGNAL_SPOT_SUBMITTED,
  SIGNAL_SEARCH_CHANGED,
  SIGNAL_MODE_FILTER_CHANGED,
  SIGNAL_TUNE_FREQUENCY,
  N_SIGNALS
};

G_BEGIN_DECLS
#define ARTEMIS_TYPE_APP (artemis_app_get_type())

G_DECLARE_FINAL_TYPE(ArtemisApp, artemis_app, ARTEMIS, APP, AdwApplication)

GtkWindow*
artemis_build_ui(ArtemisApp* self, GtkApplication* app);

void
artemis_app_reload_ui(ArtemisApp* self);

GSettings *artemis_app_get_settings();

void
artemis_app_emit_spot_submitted(ArtemisApp *app, ArtemisSpot *spot);

void
artemis_app_emit_search_changed(ArtemisApp *app, const gchar *search_text);

void
artemis_app_emit_mode_filter_changed(ArtemisApp *app, const gchar *mode);

void
artemis_app_emit_tune_frequency(ArtemisApp *app, guint64 frequency_khz, ArtemisSpot *spot);

ArtemisSpot *artemis_app_get_pinned_spot(ArtemisApp *app);

struct _ArtemisSpotRepo *artemis_app_get_spot_repo(ArtemisApp *app);

G_END_DECLS