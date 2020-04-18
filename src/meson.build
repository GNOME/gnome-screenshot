sources = [
  'gnome-screenshot.c',

  'cheese-flash.c',

  'screenshot-application.c',
  'screenshot-area-selection.c',
  'screenshot-backend.c',
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
  x11_dep,
  xext_dep,
  glib_dep,
  gtk_dep,
  canberra_dep
]

c_args = [
  '-DLOCALEDIR="@0@"'.format(gnome_screenshot_localedir),
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
  '-DGDK_DISABLE_DEPRECATION_WARNINGS',
]

executable(
  'gnome-screenshot',
  sources + resources,
  include_directories: include_directories,
  dependencies: dependencies,
  c_args: c_args,
  install: true
)
