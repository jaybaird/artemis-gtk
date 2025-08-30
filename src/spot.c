#include "spot.h"

#include "glib.h"
#include "utils.h"

struct _ArtemisSpot {
  GObject    parent_instance;
  char      *callsign;
  char      *park_ref;
  char      *park_name;
  char      *mode;
  char      *band;          /* derived from frequency_hz */
  int        frequency_hz;
  GDateTime *spot_time;   /* non-null if parsed */
  int        spot_count;

  char      *location_desc;
  char      *activator_comment;
  char      *spotter;
  char      *spotter_comment;
};

G_DEFINE_FINAL_TYPE(ArtemisSpot, artemis_spot, G_TYPE_OBJECT)

static const char *obj_str(JsonObject *o, const char *k) {
  return json_object_has_member(o,k) ? json_object_get_string_member(o,k) : "";
}

static int obj_int(JsonObject *o, const char *k, int defv) {
  return json_object_has_member(o,k) ? (int)json_object_get_int_member(o,k) : defv;
}

static GDateTime *parse_dt(JsonObject *o) {
  const char *iso = obj_str(o, "spotTime");
  if (!iso) return NULL;
  GDateTime *dt = g_date_time_new_from_iso8601(iso, g_time_zone_new_utc());

  return dt;
}

static void artemis_spot_dispose(GObject *obj) {
  ArtemisSpot *self = (ArtemisSpot*)obj;
  g_clear_pointer(&self->callsign, g_free);
  g_clear_pointer(&self->park_ref, g_free);
  g_clear_pointer(&self->park_name, g_free);
  g_clear_pointer(&self->mode, g_free);
  g_clear_pointer(&self->band, g_free);
  g_clear_pointer(&self->location_desc, g_free);
  g_clear_pointer(&self->activator_comment, g_free);
  g_clear_pointer(&self->spotter, g_free);
  g_clear_pointer(&self->spotter_comment, g_free);
  g_date_time_unref(self->spot_time);

  G_OBJECT_CLASS(artemis_spot_parent_class)->dispose(obj);
}

static void artemis_spot_class_init(ArtemisSpotClass *klass) {
  GObjectClass *oc = G_OBJECT_CLASS(klass);
  oc->dispose = artemis_spot_dispose;
}
static void artemis_spot_init(ArtemisSpot *self) {}

ArtemisSpot *artemis_spot_new(const char    *callsign,
                              const char    *park_ref,
                              const char    *park_name,
                              const char    *location_desc,
                              const char    *activator_comment,
                              int            frequency_hz,
                              const char    *mode,
                              GDateTime     *spot_time,
                              const char    *spotter,
                              const char    *spotter_comment,
                              int            spot_count
)
{
  ArtemisSpot *self = g_object_new(ARTEMIS_TYPE_SPOT, NULL);
  self->callsign     = g_strdup(callsign);
  self->park_ref     = g_strdup(park_ref);
  self->park_name    = g_strdup(park_name);
  self->mode         = g_strdup(mode);
  self->frequency_hz = frequency_hz;
  self->band         = g_strdup(band_from_hz(frequency_hz));
  self->spot_time    = spot_time ? g_date_time_ref(spot_time) : NULL;
  self->spot_count   = spot_count;
  self->location_desc = g_strdup(location_desc);
  self->activator_comment = g_strdup(activator_comment);
  self->spotter      = g_strdup(spotter);
  self->spotter_comment = g_strdup(spotter_comment);
  return self;
}

ArtemisSpot *artemis_spot_new_from_json(JsonObject *o) {
  const char *callsign = obj_str(o, "activator");
  const char *park_ref = obj_str(o, "reference");
  const char *park_name = obj_str(o, "name");
  const char *mode = obj_str(o, "mode");
  const char *location_desc = obj_str(o, "locationDesc");
  const char *activator_comment = obj_str(o, "activatorLastComments");
  const char *spotter = obj_str(o, "spotter");
  const char *spotter_comment = obj_str(o, "comments");

  int freq_hz = atoi(obj_str(o, "frequency"));
  GDateTime *dt  = parse_dt(o);
  int count      = obj_int(o, "count", 0);

  ArtemisSpot *spot = artemis_spot_new(
    callsign, 
    park_ref, 
    park_name, 
    location_desc,
    activator_comment, 
    freq_hz, 
    mode, 
    dt, 
    spotter, 
    spotter_comment, 
    count);
  return spot;
}

/* Getters */
const char *artemis_spot_get_callsign    (ArtemisSpot *s){ return s->callsign; }
const char *artemis_spot_get_park_ref    (ArtemisSpot *s){ return s->park_ref; }
const char *artemis_spot_get_park_name   (ArtemisSpot *s){ return s->park_name; }
const char *artemis_spot_get_mode        (ArtemisSpot *s){ return s->mode; }
const char *artemis_spot_get_band        (ArtemisSpot *s){ return s->band; }
const char *artemis_spot_get_location_desc(ArtemisSpot *s){ return s->location_desc; };
const char *artemis_spot_get_spotter(ArtemisSpot *s) { return s->spotter; };
const char *artemis_spot_get_spotter_comment(ArtemisSpot *s) { return s->spotter_comment; };
const char *artemis_spot_get_activator_comment(ArtemisSpot *s) { return s->activator_comment; };
int         artemis_spot_get_frequency_hz(ArtemisSpot *s){ return s->frequency_hz; }
GDateTime  *artemis_spot_get_spot_time (ArtemisSpot *s){ return s->spot_time; }
int         artemis_spot_get_spot_count  (ArtemisSpot *s){ return s->spot_count; }


/* Store helpers */
GListStore *artemis_spot_store_new(void) {
  return g_list_store_new(ARTEMIS_TYPE_SPOT);
}

void artemis_spot_store_add_json_array(GListStore *store, JsonArray *arr) {
  g_return_if_fail(G_IS_LIST_STORE(store));
  g_return_if_fail(arr != NULL);

  guint n = json_array_get_length(arr);
  for (guint i = 0; i < n; ++i) {
    JsonObject *o = json_array_get_object_element(arr, i);
    if (!o) continue;
    ArtemisSpot *spot = artemis_spot_new_from_json(o);
    g_list_store_append(store, spot);
    g_object_unref(spot);
  }
}