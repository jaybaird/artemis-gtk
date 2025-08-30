#pragma once

#include <glib-object.h>
#include <json-glib/json-glib.h>
#include "glib.h"
#include "spot.h"

G_BEGIN_DECLS

#define POTA_TYPE_CLIENT (pota_client_get_type())
G_DECLARE_FINAL_TYPE(PotaClient, pota_client, ARTEMIS, POTA_CLIENT, GObject)

PotaClient *pota_client_new(void);

void        pota_client_set_timeout     (PotaClient *self, guint io_timeout_sec, guint idle_timeout_sec);

void        pota_client_post_spot_async (PotaClient        *self,
                                         ArtemisSpot       *spot,
                                         GCancellable      *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer           user_data);

JsonNode   *pota_client_post_spot_finish(PotaClient   *self,
                                         GAsyncResult *res,
                                         GError      **error);

void      pota_client_get_spots_async (PotaClient         *self,
                                       GCancellable       *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer            user_data);

JsonNode* pota_client_get_spots_finish(PotaClient   *self,
                                       GAsyncResult *res,
                                       GError      **error);

void      pota_client_get_activator_async (PotaClient         *self,
                                           const gchar        *callsign,
                                           GCancellable       *cancellable,
                                           GAsyncReadyCallback callback,
                                           gpointer            user_data);

JsonNode* pota_client_get_activator_finish(PotaClient   *self,
                                           GAsyncResult *res,
                                           GError      **error);
G_END_DECLS
