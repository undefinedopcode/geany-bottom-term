project('geany-bottom-term', 'c',
  version: '0.1.0',
  meson_version: '>= 0.60',
  default_options: ['c_std=c11', 'warning_level=2'],
)

geany_dep = dependency('geany', version: '>= 2.0')
gtk_dep = dependency('gtk+-3.0')
vte_dep = dependency('vte-2.91')

plugin_sources = files(
  'src/plugin.c',
  'src/terminal.c',
  'src/tab_manager.c',
  'src/reparent.c',
  'src/colors.c',
)

shared_library('geany-bottom-term',
  plugin_sources,
  dependencies: [geany_dep, gtk_dep, vte_dep],
  c_args: [
    '-DPLUGIN_VERSION="@0@"'.format(meson.project_version()),
  ],
  install: true,
  install_dir: geany_dep.get_variable(pkgconfig: 'libdir') / 'geany',
  name_prefix: '',
)
