gnome = import('gnome')

sources = [
  'gnome-screenshot.c',

  'cheese-flash.c',

  'screenshot-application.c',
  'screenshot-area-selection.c',
  'screenshot-config.c',
  'screenshot-dialog.c',
  'screenshot-filename-builder.c',
  'screenshot-interactive-dialog.c',
  'screenshot-shadow.c',
  'screenshot-utils.c',
]

resources = gnome.compile_resources('screenshot-resources',
                                    'gnome-screenshot.gresource.xml',
                                    source_dir: '.',
                                    c_name: 'screenshot')

executable('gnome-screenshot', sources + resources,
           include_directories: [ root_inc, include_directories('.') ],
           dependencies: [ mathlib_dep, x11_dep, glib_dep, gtk_dep, canberra_dep ],
           c_args: [
             '-DLOCALEDIR="@0@"'.format(gnome_screenshot_localedir),
             '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
             '-DGDK_DISABLE_DEPRECATION_WARNINGS',
           ],
           install: true)

i18n.merge_file('desktop',
                type: 'desktop',
                input: 'org.gnome.Screenshot.desktop.in',
                output: 'org.gnome.Screenshot.desktop',
                po_dir: join_paths(meson.current_source_dir(), '../po'),
                install: true,
                install_dir: gnome_screenshot_appsdir)

i18n.merge_file('appdata',
                input: 'org.gnome.Screenshot.metainfo.xml.in',
                output: 'org.gnome.Screenshot.metainfo.xml',
                po_dir: join_paths(meson.current_source_dir(), '../po'),
                install: true,
                install_dir: gnome_screenshot_appdatadir)

service_conf = configuration_data ()
service_conf.set('bindir', gnome_screenshot_bindir)
configure_file(input: 'org.gnome.Screenshot.service.in',
               output: 'org.gnome.Screenshot.service',
               configuration: service_conf,
               install: true,
               install_dir: gnome_screenshot_servicesdir)

install_data('org.gnome.gnome-screenshot.gschema.xml',
             install_dir: gnome_screenshot_schemadir)

install_data('gnome-screenshot.1',
             install_dir: join_paths(get_option('mandir'), 'man1'))

install_data('gnome-screenshot.convert',
             install_dir: join_paths(gnome_screenshot_datadir, 'GConf', 'gsettings'))
