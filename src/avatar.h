#include <glib.h>
#include <adwaita.h>
#include <libdex.h>
#include <libsoup/soup.h>

typedef struct {
  AdwAvatar *target_avatar;
  gchar *callsign; // for gravatar fallback
} AvatarUpdateData;

void
avatar_update_data_free(AvatarUpdateData *data);

DexFuture *
avatar_fetch_gravatar_async(gchar *gravatar_hash, AdwAvatar *avatar, gchar *callsign);