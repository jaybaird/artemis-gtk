#pragma once

#include <adwaita.h>

#include <adwaita.h>
#include <gtk/gtk.h>

#define SCHEMA_ID "com.k0vcz.artemis"

gboolean spot_preferences_is_configured();
void show_preferences_dialog(GtkWidget *parent);
