#include "activator.h"
#include <json-glib/json-glib.h>

struct _ArtemisActivator {
  GObject parent_instance;
  
  gchar *callsign;
  gchar *name;
  gchar *qth;
  gchar *gravatar_hash;
  int    activations;
  int    parks;
  int    qsos;
};

G_DEFINE_FINAL_TYPE(ArtemisActivator, artemis_activator, G_TYPE_OBJECT)

static void
artemis_activator_finalize(GObject *object) {
  ArtemisActivator *self = ARTEMIS_ACTIVATOR(object);
  
  g_clear_pointer(&self->callsign, g_free);
  g_clear_pointer(&self->name, g_free);
  g_clear_pointer(&self->qth, g_free);
  g_clear_pointer(&self->gravatar_hash, g_free);
  
  G_OBJECT_CLASS(artemis_activator_parent_class)->finalize(object);
}

static void
artemis_activator_class_init(ArtemisActivatorClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = artemis_activator_finalize;
}

static void
artemis_activator_init(ArtemisActivator *self) {
  self->callsign = NULL;
  self->name = NULL;
  self->qth = NULL;
  self->gravatar_hash = NULL;
  self->activations = 0;
  self->parks = 0;
  self->qsos = 0;
}

ArtemisActivator *artemis_activator_new(
  const char    *callsign,
  const char    *name,
  const char    *qth,
  const char    *gravatar_hash,
  int            activations,
  int            parks,
  int            qsos
) {
  ArtemisActivator *self = g_object_new(ARTEMIS_TYPE_ACTIVATOR, NULL);
  
  self->callsign = g_strdup(callsign);
  self->name = g_strdup(name);
  self->qth = g_strdup(qth);
  self->gravatar_hash = g_strdup(gravatar_hash);
  self->activations = activations;
  self->parks = parks;
  self->qsos = qsos;
  
  return self;
}

ArtemisActivator *
artemis_activator_new_from_json(JsonObject *obj) {
  g_return_val_if_fail(obj != NULL, NULL);
  
  const char *callsign = json_object_get_string_member_with_default(obj, "callsign", "");
  const char *name = json_object_get_string_member_with_default(obj, "name", "");
  const char *qth = json_object_get_string_member_with_default(obj, "qth", "");
  const char *gravatar_hash = json_object_get_string_member_with_default(obj, "gravatar", "");
  
  int activations = (int)json_object_get_int_member_with_default(obj, "activations", 0);
  int parks = (int)json_object_get_int_member_with_default(obj, "parks", 0);
  int qsos = (int)json_object_get_int_member_with_default(obj, "qsos", 0);
  
  return artemis_activator_new(callsign, name, qth, gravatar_hash, activations, parks, qsos);
}

gchar *
artemis_activator_get_callsign(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), NULL);
  return self->callsign;
}

gchar *
artemis_activator_get_name(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), NULL);
  return self->name;
}

gchar *
artemis_activator_get_qth(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), NULL);
  return self->qth;
}

gchar *
artemis_activator_get_gravatar_hash(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), NULL);
  return self->gravatar_hash;
}

int
artemis_activator_get_activations(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), 0);
  return self->activations;
}

int
artemis_activator_get_parks(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), 0);
  return self->parks;
}

int
artemis_activator_get_qsos(ArtemisActivator *self) {
  g_return_val_if_fail(ARTEMIS_IS_ACTIVATOR(self), 0);
  return self->qsos;
}