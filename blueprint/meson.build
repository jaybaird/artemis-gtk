blueprint = find_program('blueprint-compiler', required: true)

inputs = files(
  'add_spot_page.blp',
  'main_window.blp',
  'preferences.blp',
  'spot_card.blp',
  'spot_history_dialog.blp',
  'spot_page.blp',
  'status_page.blp',
)

outs = [
  'add_spot_page.ui',
  'main_window.ui',
  'preferences.ui',
  'spot_card.ui',
  'spot_history_dialog.ui',
  'spot_page.ui',
  'status_page.ui',
]

blueprint_batch = custom_target(
  'blueprints compile',
  input: inputs,
  output: outs,
  command: [
    blueprint, 'batch-compile',
    meson.current_build_dir(),    
    meson.current_source_dir(),   
    '@INPUT@'
  ],
  build_by_default: true,
  install: false, 
)

artemis_ui = blueprint_batch
