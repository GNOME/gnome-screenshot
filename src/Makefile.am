NULL =

AM_CPPFLAGS =							\
	-I.							\
	-I$(srcdir)						\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DUIDIR=\""$(uidir)"\"					\
	$(NULL)

bin_PROGRAMS = \
	gnome-screenshot		\
	$(NULL)

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-screenshot.gresource.xml)
screenshot-resources.c: gnome-screenshot.gresource.xml $(resource_files)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name screenshot $(srcdir)/gnome-screenshot.gresource.xml
screenshot-resources.h: gnome-screenshot.gresource.xml $(resource_files)
	$(AM_V_GEN)glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name screenshot $(srcdir)/gnome-screenshot.gresource.xml

BUILT_SOURCES = \
	screenshot-resources.c		\
	screenshot-resources.h		\
	$(NULL)

gnome_screenshot_SOURCES = \
	$(BUILT_SOURCES)		\
	gnome-screenshot.c		\
	cheese-flash.c			\
	cheese-flash.h			\
	screenshot-application.c	\
	screenshot-application.h	\
	screenshot-area-selection.c	\
	screenshot-area-selection.h	\
	screenshot-config.c		\
	screenshot-config.h		\
	screenshot-dialog.c		\
	screenshot-dialog.h		\
	screenshot-filename-builder.c   \
	screenshot-filename-builder.h   \
	screenshot-interactive-dialog.c \
	screenshot-interactive-dialog.h \
	screenshot-shadow.c		\
	screenshot-shadow.h		\
	screenshot-utils.c		\
	screenshot-utils.h		\
	$(NULL)

gnome_screenshot_CFLAGS =		\
	$(SCREENSHOT_CFLAGS)		\
	$(XSHAPE_CFLAGS)		\
	$(NULL)

gnome_screenshot_LDADD =		\
	$(SCREENSHOT_LIBS)		\
	$(XSHAPE_LIBS)			\
	-lm				\
	$(NULL)

gnome_screenshotdir = $(datadir)/applications
gnome_screenshot_in_files = org.gnome.Screenshot.desktop.in
gnome_screenshot_DATA = $(gnome_screenshot_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = org.gnome.Screenshot.appdata.xml.in
@INTLTOOL_XML_RULE@

%.gschema.xml: %.gschema.xml.in
	$(AM_V_GEN) $(INTLTOOL_MERGE) -x -u --no-translations $< $@

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_DATA = org.gnome.Screenshot.service

org.gnome.Screenshot.service: Makefile
	$(AM_V_GEN) (echo '[D-BUS Service]'; \
		     echo 'Name=org.gnome.Screenshot'; \
		     echo 'Exec=${bindir}/gnome-screenshot --gapplication-service') > $@.tmp && \
		     mv $@.tmp $@

man_MANS = gnome-screenshot.1

gsettingsschema_in_files = org.gnome.gnome-screenshot.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)
.PRECIOUS: $(gsettings_SCHEMAS)

convertdir = $(datadir)/GConf/gsettings
convert_DATA = gnome-screenshot.convert

@GSETTINGS_RULES@

EXTRA_DIST =					\
	$(appdata_in_files)			\
	$(gsettingsschema_in_files)		\
	$(gnome_screenshot_in_files)		\
	$(man_MANS)				\
	$(convert_DATA)				\
	$(resource_files)			\
	gnome-screenshot.gresource.xml		\
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES)			\
	$(appdata_DATA)				\
	$(gsettings_SCHEMAS)			\
	$(gnome_screenshot_DATA)		\
	$(dbusservice_DATA)			\
	$(NULL)

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)
