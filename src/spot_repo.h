#include "pota_client.h"
#include "pota_user_cache.h"
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define ARTEMIS_TYPE_SPOT_REPO (artemis_spot_repo_get_type())
G_DECLARE_FINAL_TYPE(ArtemisSpotRepo, artemis_spot_repo, ARTEMIS, SPOT_REPO, GObject)

ArtemisSpotRepo *artemis_spot_repo_new();

GListModel *artemis_spot_repo_get_model(ArtemisSpotRepo *self);

gboolean
artemis_spot_repo_get_busy(ArtemisSpotRepo *self);

PotaClient *artemis_spot_repo_get_pota_client(ArtemisSpotRepo *self);
ArtemisPotaUserCache *artemis_spot_repo_get_pota_user_cache(ArtemisSpotRepo *self);
void
artemis_spot_repo_update_spots(ArtemisSpotRepo *self, guint ttl_secs);

G_END_DECLS
