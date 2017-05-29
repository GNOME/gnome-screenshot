project('gnome-screenshot', 'c',
        version: '3.25.0',
        license: 'GPLv2+',
        meson_version: '>= 0.40.1',
        default_options: [
          'buildtype=debugoptimized',
          'warning_level=1',
          'c_std=c99',
        ])

add_project_arguments([ '-D_POSIX_C_SOURCE', '-D_DEFAULT_SOURCE' ], language: 'c')

cc = meson.get_compiler('c')

gnome_screenshot_prefix = get_option('prefix')
gnome_screenshot_bindir = join_paths(gnome_screenshot_prefix, get_option('bindir'))
gnome_screenshot_datadir = join_paths(gnome_screenshot_prefix, get_option('datadir'))
gnome_screenshot_libexecdir = join_paths(gnome_screenshot_prefix, get_option('libexecdir'))
gnome_screenshot_localedir = join_paths(gnome_screenshot_datadir, 'locale')
gnome_screenshot_appsdir = join_paths(gnome_screenshot_datadir, 'applications')
gnome_screenshot_appdatadir = join_paths(gnome_screenshot_datadir, 'metainfo')
gnome_screenshot_servicesdir = join_paths(gnome_screenshot_datadir, 'dbus-1', 'services')
gnome_screenshot_schemadir = join_paths(gnome_screenshot_datadir, 'glib-2.0', 'schemas')

glib_req_version = '>= 2.35.1'
gtk_req_version = '>= 3.0.0'

mathlib_dep = cc.find_library('m', required: false)
x11_dep = [ dependency('x11'), dependency('xext') ]
glib_dep = dependency('glib-2.0', version: glib_req_version)
gtk_dep = dependency('gtk+-3.0', version: gtk_req_version)
canberra_dep = dependency('libcanberra-gtk3')

config_h = configuration_data()
config_h.set_quoted('VERSION', meson.project_version())
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())

if cc.has_header('X11/extensions/shape.h')
  config_h.set('HAVE_X11_EXTENSIONS_SHAPE_H', 1)
endif

configure_file(output: 'config.h', configuration: config_h)

root_inc = include_directories('.')

i18n = import('i18n')

subdir('src')
subdir('po')

meson.add_install_script('build-aux/postinstall.py')
