#include "glib-object.h"
#include "radio_control.h"

#include <assert.h>

#define return_if_error(err) \
  G_STMT_START { \
    if (error) \
      return dex_future_new_for_error(error); \
  } G_STMT_END

static DexFuture *
test_main(gpointer user_data)
{
  RadioControl *radio = ARTEMIS_RADIO_CONTROL(user_data);
  g_autoptr(GError) error = NULL;

  g_message("Attempting to connect to radio...");
  DexFuture *connect = radio_control_connect_async(radio);
  dex_await_boolean(connect, &error);
  
  return_if_error(error);

  g_message("Connected successfully, now attempting to get frequency");

  DexFuture *freq_result = radio_control_get_vfo_async(radio);
  int freq = dex_await_int(freq_result, &error);
  int old_freq = freq;
  
  return_if_error(error);

  g_message("Got frequency: %d", freq);
  
  freq_result = radio_control_set_vfo_async(radio, 14347);
  dex_await_boolean(freq_result, &error);

  return_if_error(error);

  freq_result = radio_control_get_vfo_async(radio);
  freq = dex_await_int(freq_result, &error);

  return_if_error(error);

  assert(freq == 14347);

  DexFuture *mode_future = radio_control_get_mode_async(radio);
  enum RadioMode old_mode = (enum RadioMode)dex_await_int(mode_future, &error);

  return_if_error(error);

  mode_future = radio_control_set_mode_async(radio, DIGITAL_L);
  dex_await_boolean(mode_future, &error);

  return_if_error(error);

  mode_future = radio_control_get_mode_async(radio);
  enum RadioMode mode = (enum RadioMode)dex_await_int(mode_future, &error);

  return_if_error(error);

  assert(mode == DIGITAL_L);

  return dex_future_all(
    radio_control_set_mode_async(radio, old_mode),
    radio_control_set_vfo_async(radio, old_freq),
    NULL
  );
}

static DexFuture *
quit_main_loop(DexFuture *future, gpointer user_data)
{
  GMainLoop *main_loop = (GMainLoop*)user_data;
  g_main_loop_quit(main_loop);
  return dex_future_new_true();
}

int main(int argc, char **argv)
{
  GMainLoop *main_loop;
  
  dex_init();
  RadioControl *radio = radio_control_new();
  DexFuture *test_future = dex_scheduler_spawn(dex_scheduler_get_default(), 0, test_main, g_object_ref(radio), g_object_unref);

  main_loop = g_main_loop_new(NULL, FALSE);

  dex_future_then(test_future, quit_main_loop, main_loop, NULL);

  g_main_loop_run(main_loop);

  g_message("Test completed");
  
  g_clear_object(&test_future);
  g_clear_object(&radio);

  g_main_loop_unref(main_loop);
  return 0;
}