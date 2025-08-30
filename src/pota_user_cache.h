#pragma once

#include "activator.h"
#include "pota_client.h"
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ARTEMIS_TYPE_POTA_USER_CACHE (artemis_pota_user_cache_get_type())
G_DECLARE_FINAL_TYPE(ArtemisPotaUserCache, artemis_pota_user_cache, ARTEMIS, POTA_USER_CACHE, GObject)

typedef struct _PotaUserCacheEntry PotaUserCacheEntry;

ArtemisPotaUserCache *artemis_pota_user_cache_new(PotaClient *client);

void artemis_pota_user_cache_get_async(ArtemisPotaUserCache *self,
                                       const gchar          *callsign,
                                       guint                 ttl_seconds,
                                       GCancellable         *cancellable,
                                       GAsyncReadyCallback   callback,
                                       gpointer              user_data);

ArtemisActivator *artemis_pota_user_cache_get_finish(ArtemisPotaUserCache *self,
                                                     GAsyncResult         *result,
                                                     GError              **error);

void artemis_pota_user_cache_clear(ArtemisPotaUserCache *self);
void artemis_pota_user_cache_set_ttl_default(ArtemisPotaUserCache *self, guint ttl_seconds);

// Singleton access (initialized when first spot repo is created)
ArtemisPotaUserCache *artemis_pota_user_cache_get_instance(void);
void artemis_pota_user_cache_cleanup_instance(void);

G_END_DECLS