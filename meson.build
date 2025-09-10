project('Artemis', 'c',
  version: '1.0',
  meson_version: '>= 1.4.0',
  default_options: ['c_std=c11', 'buildtype=debug']
)

app_id = 'com.k0vcz.artemis'

cc = meson.get_compiler('c')

deps = [
  dependency('gtk4', required: true),
  dependency('libadwaita-1', required: true),
  dependency('gio-2.0', required: true),
  dependency('json-glib-1.0', required: true),
  dependency('libsoup-3.0', required: true),
  dependency('hamlib', required: true),
  dependency('libdex-1', required: true),
  dependency('sqlite3'),
  cc.find_library('m')
]

gnome = import('gnome')
fs = import('fs')
i18n = import('i18n')

subdir('blueprint')
subdir('data')
subdir('po')

resources = gnome.compile_resources(
  'artemis-resources',
  resource_xml,
  source_dir: meson.current_build_dir() / 'blueprint',
  dependencies: artemis_ui,
  c_name: 'artemis'
)

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)

i18n.gettext(meson.project_name(), preset: 'glib', install: true)

app_id = 'com.k0vcz.artemis'
conf = configuration_data()
conf.set_quoted('APP_ID', app_id)
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
configure_file(output: 'config.h', configuration: conf)

add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

executable('artemis',
  sources: [
    'src/main.c',
    'src/artemis.c',
    'src/pota_client.c',
    'src/spot_card.c',
    'src/spot_history_dialog.c',
    'src/database.c',
    'src/preferences.c',
    'src/utils.c',
    'src/spot.c',
    'src/spot_repo.c',
    'src/status_page.c',
    'src/spot_page.c',
    'src/activator.c',
    'src/pota_user_cache.c',
    'src/avatar.c',
    'src/logbook.c',
    'src/logbook_qrz.c',
    'src/dex_soup.c',
    'src/radio_control.c',
    resources,
  ],
  dependencies: deps,
  install: true,
  include_directories: include_directories('src')
)
