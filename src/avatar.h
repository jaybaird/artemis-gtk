#include <glib.h>
#include <adwaita.h>
#include <libsoup/soup.h>

typedef struct {
  AdwAvatar *target_avatar;
  gchar *callsign; // for gravatar fallback
} AvatarUpdateData;

void
avatar_update_data_free(AvatarUpdateData *data);

void
avatar_fetch_gravatar_async(const char *gravatar_hash, AvatarUpdateData *data);