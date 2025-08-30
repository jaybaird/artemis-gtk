#!/bin/sh

set -e

printf "Compiling schemas..."
glib-compile-schemas --strict \
  --targetdir build/schemas \
  data

printf "Running blueprint compiler..."
blueprint-compiler batch-compile build . blueprint/main_window.blp blueprint/spot_card.blp blueprint/preferences.blp blueprint/status_page.blp blueprint/spot_page.blp blueprint/add_spot_page.blp &&

#printf "\nRunning meson setup...\n"
#meson setup --reconfigure build && 

printf "\nRunning meson build...\n"
meson compile -C build
#ninja -C build install
