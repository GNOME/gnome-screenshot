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

# Subdirectories
subdir('icons')
