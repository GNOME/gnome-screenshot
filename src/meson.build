sources = [
  'gnome-screenshot.c',

  'screenshot-application.c',
  'screenshot-area-selection.c',
  'screenshot-backend.c',
  'screenshot-backend-shell.c',
  'screenshot-config.c',
  'screenshot-dialog.c',
  'screenshot-filename-builder.c',
  'screenshot-interactive-dialog.c',
  'screenshot-utils.c',
]

include_directories = [
  root_inc,
  include_directories('.')
]

dependencies = [
  mathlib_dep,
  glib_dep,
  gtk_dep,
  libhandy_dep,
  gsound_dep
]

c_args = [
  '-DLOCALEDIR="@0@"'.format(gnome_screenshot_localedir),
]

executable(
  'gnome-screenshot',
  sources + resources,
  include_directories: include_directories,
  dependencies: dependencies,
  c_args: c_args,
  install: true
)
