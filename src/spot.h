#pragma once

#include <glib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define ARTEMIS_TYPE_SPOT (artemis_spot_get_type())
G_DECLARE_FINAL_TYPE(ArtemisSpot, artemis_spot, ARTEMIS, SPOT, GObject)

ArtemisSpot *artemis_spot_new(
  const char    *callsign,
  const char    *park_ref,
  const char    *park_name,
  const char    *location_desc,
  const char    *activator_comment,
  int            frequency_hz,
  const char    *mode,
  GDateTime     *created_utc,
  const char    *spotter,
  const char    *spotter_comment,
  int            spot_count
);

ArtemisSpot *artemis_spot_new_from_json(JsonObject *obj);

/* Getters */
const char *artemis_spot_get_callsign     (ArtemisSpot *self);
const char *artemis_spot_get_park_ref     (ArtemisSpot *self);
const char *artemis_spot_get_park_name    (ArtemisSpot *self);
const char *artemis_spot_get_location_desc(ArtemisSpot *self);
const char *artemis_spot_get_mode         (ArtemisSpot *self);
const char *artemis_spot_get_band         (ArtemisSpot *self);
int         artemis_spot_get_frequency_hz (ArtemisSpot *self);
GDateTime  *artemis_spot_get_spot_time    (ArtemisSpot *self); /* borrowed */
int         artemis_spot_get_spot_count   (ArtemisSpot *self);
const char *artemis_spot_get_spotter      (ArtemisSpot *self);

const char *artemis_spot_get_spotter_comment  (ArtemisSpot *self);
const char *artemis_spot_get_activator_comment(ArtemisSpot *self);

/* Store helpers (backed by GListStore<ArtemisSpot>) */
GListStore *artemis_spot_store_new(void);
void        artemis_spot_store_add_json_array(GListStore *store, JsonArray *arr);


G_END_DECLS