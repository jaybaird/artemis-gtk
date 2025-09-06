data_conf = configuration_data()
data_conf.set('appid', app_id)

desktop_file = configure_file(
        input: 'com.k0vcz.artemis.desktop.in',
       output: 'com.k0vcz.artemis.desktop',
      install: true,
configuration: data_conf,
  install_dir: get_option('datadir') / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils, args: [desktop_file])
endif

metainfo_tmpl = configure_file(
  input: 'com.k0vcz.artemis.metainfo.xml.in',
  output: 'com.k0vcz.artemis.metainfo.xml.in',
  configuration: data_conf
)

appstream_file = i18n.merge_file(
  input: metainfo_tmpl,
  output: 'com.k0vcz.artemis.metainfo.xml',
  po_dir: meson.project_source_root() / 'po',
  install: true,
  install_dir: get_option('datadir') / 'metainfo',
  type: 'xml'
)

appstreamcli = find_program('appstreamcli', required: false, disabler: true)
test('Validate appstream file', appstreamcli,
     args: ['validate', '--no-net', '--explain', appstream_file])

install_data('com.k0vcz.artemis.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

gnome.compile_schemas(build_by_default: true, depend_files: files('com.k0vcz.artemis.gschema.xml'))

resource_xml = configure_file(
  input: 'artemis.gresource.xml.in',
  output: 'artemis.gresource.xml',
  configuration: {
    'build_dir': meson.project_build_root()
  }
)

subdir('icons')