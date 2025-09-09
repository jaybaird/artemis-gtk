#include <glib.h>
#include <hamlib/rig.h>
#include <libdex.h>

G_BEGIN_DECLS

#define ARTEMIS_TYPE_RADIO_CONTROL (radio_control_get_type())

G_DECLARE_FINAL_TYPE(RadioControl, radio_control, ARTEMIS, RADIO_CONTROL, GObject)

enum RadioMode {
  UNKNOWN, 
  CW, 
  CW_R, 
  USB, 
  LSB,
  DIGITAL_U, 
  DIGITAL_L, 
  AM, 
  FM,
  DIGITAL_FM
};

enum RadioStatusSignal {
  SIG_CONNECTED,
  SIG_DISCONNECTED,
  SIG_STATUS,
  SIG_ERROR,
  N_RIG_SIGNALS
};

RadioControl *radio_control_new();

DexFuture *
radio_control_connect_async(RadioControl *self);

DexFuture *
radio_control_disconnect_async(RadioControl *self);

/* Getters */

gboolean
radio_control_is_rig_connected(RadioControl *self);

DexFuture *
radio_control_get_vfo_async(RadioControl *self);

DexFuture *
radio_control_get_mode_async(RadioControl *self);

/* Setters */

DexFuture *
radio_control_set_vfo_async(RadioControl *self, int frequency);

DexFuture *
radio_control_set_mode_async(RadioControl *self, enum RadioMode mode);

G_END_DECLS