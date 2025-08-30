#pragma once

#include <adwaita.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS
#define ARTEMIS_TYPE_SPOT_CARD (spot_card_get_type())

G_DECLARE_FINAL_TYPE(SpotCard, spot_card, ARTEMIS, SPOT_CARD, GtkBox)

SpotCard *spot_card_new(void);
GtkWidget *spot_card_new_from_spot(gpointer user_data); // for GtkFlowBoxCreateWidgetFunc
void spot_card_set_corner_image_visible(SpotCard *self, gboolean visible);

G_END_DECLS