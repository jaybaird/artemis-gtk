#pragma once

#include <glib.h>
#include <glib-object.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define ARTEMIS_TYPE_ACTIVATOR (artemis_activator_get_type())
G_DECLARE_FINAL_TYPE(ArtemisActivator, artemis_activator, ARTEMIS, ACTIVATOR, GObject)

ArtemisActivator *artemis_activator_new(
  const char    *callsign,
  const char    *name,
  const char    *qth,
  const char    *gravatar_hash,
  int            activations,
  int            parks,
  int            qsos
);

ArtemisActivator *artemis_activator_new_from_json(JsonObject *obj);

/* Getters */
const char *artemis_activator_get_callsign      (ArtemisActivator *self);
const char *artemis_activator_get_name          (ArtemisActivator *self);
const char *artemis_activator_get_qth           (ArtemisActivator *self);
const char *artemis_activator_get_gravatar_hash (ArtemisActivator *self);
int         artemis_activator_get_activations   (ArtemisActivator *self);
int         artemis_activator_get_parks         (ArtemisActivator *self);
int         artemis_activator_get_qsos          (ArtemisActivator *self);

G_END_DECLS