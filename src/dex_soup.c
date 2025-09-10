#include "dex_soup.h"

static void
session_send_cb (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  DexAsyncPair *async_pair = user_data;
  GInputStream *stream;
  GError *error = NULL;

  stream = soup_session_send_finish (SOUP_SESSION (object), result, &error);

  if (error == NULL)
    dex_async_pair_return_object (async_pair, stream);
  else
    dex_async_pair_return_error (async_pair, error);

  dex_unref (async_pair);
}

DexFuture *
artemis_session_send(SoupSession *session,
                     SoupMessage *message,
                     int          priority)
{
  DexAsyncPair *async_pair;

  g_return_val_if_fail (SOUP_IS_SESSION (session), NULL);
  g_return_val_if_fail (SOUP_IS_MESSAGE (message), NULL);

  async_pair = (DexAsyncPair *)g_type_create_instance (DEX_TYPE_ASYNC_PAIR);
  soup_session_send_async(session, message, priority,
                          dex_async_pair_get_cancellable (async_pair),
                          session_send_cb,
                          dex_ref (async_pair));
  return DEX_FUTURE(async_pair);
}