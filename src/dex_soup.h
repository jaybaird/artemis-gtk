#include <libdex.h>
#include <libsoup/soup.h>

DexFuture *
artemis_session_send(SoupSession *session,
                     SoupMessage *message,
                     int          priority);