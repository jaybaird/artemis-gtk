project('Artemis', 'c',
  version: '0.1',
  meson_version: '>= 1.4.0',
  default_options: ['c_std=c11', 'buildtype=debug']
)

cc = meson.get_compiler('c')

deps = [
  dependency('gtk4', required: true),
  dependency('libadwaita-1', required: true),
  dependency('gio-2.0', required: true),
  dependency('json-glib-1.0', required: true),
  dependency('libsoup-3.0', required: true),
  dependency('hamlib', required: true),
  dependency('sqlite3'),
  cc.find_library('m')
]

gnome = import('gnome')
fs = import('fs')
i18n = import('i18n')

resources = gnome.compile_resources(
  'artemis-resources',
  'data/artemis.gresource.xml'
)

gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)

i18n.gettext(meson.project_name(), preset: 'glib', install: true)

conf = configuration_data()
conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('LOCALEDIR', join_paths(get_option('prefix'), get_option('localedir')))
configure_file(output: 'config.h', configuration: conf)

add_project_arguments('-DHAVE_CONFIG_H', language: 'c')

executable('Artemis',
  sources: [
    'src/main.c',
    'src/artemis.c',
    'src/pota_client.c',
    'src/spot_card.c',
    'src/database.c',
    'src/preferences.c',
    'src/utils.c',
    'src/spot.c',
    'src/spot_repo.c',
    'src/status_page.c',
    'src/spot_page.c',
    'src/activator.c',
    'src/pota_user_cache.c',
    resources,
  ],
  dependencies: deps,
  include_directories: include_directories('src')
)

subdir('data')
subdir('po')