desktop_file = i18n.merge_file(
        input: 'com.k0vcz.artemis.desktop.in',
       output: 'com.k0vcz.artemis.desktop',
         type: 'desktop',
       po_dir: '../po',
      install: true,
  install_dir: get_option('datadir') / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils, args: [desktop_file])
endif

appstream_file = i18n.merge_file(
        input: 'com.k0vcz.artemis.metainfo.xml.in',
       output: 'com.k0vcz.artemis.metainfo.xml',
       po_dir: '../po',
      install: true,
  install_dir: get_option('datadir') / 'metainfo'
)

appstreamcli = find_program('appstreamcli', required: false, disabler: true)
test('Validate appstream file', appstreamcli,
     args: ['validate', '--no-net', '--explain', appstream_file])

install_data('com.k0vcz.artemis.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0' / 'schemas'
)

gnome.compile_schemas(build_by_default: true, depend_files: files('com.k0vcz.artemis.gschema.xml'))

subdir('icons')