EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =

AM_CPPFLAGS =						\
	-I$(top_builddir)/src				\
	-I$(top_srcdir)/src				\
	-DLOCALEDIR=\"$(localedir)\"			\
	-DGLIB_DISABLE_DEPRECATION_WARNINGS		\
	-DGDK_DISABLE_DEPRECATION_WARNINGS		\
	$()

bin_PROGRAMS = gnome-screenshot

BUILT_SOURCES = \
	screenshot-resources.c		\
	screenshot-resources.h		\
	$()

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/gnome-screenshot.gresource.xml)
screenshot-resources.c: gnome-screenshot.gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources \
		--target=$@ \
		--sourcedir=$(srcdir) \
		--generate-source \
		--c-name screenshot \
		$(srcdir)/gnome-screenshot.gresource.xml
screenshot-resources.h: gnome-screenshot.gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources \
		--target=$@ \
		--sourcedir=$(srcdir) \
		--generate-header \
		--c-name screenshot \
		$(srcdir)/gnome-screenshot.gresource.xml

EXTRA_DIST += gnome-screenshot.gresource.xml
CLEANFILES += screenshot-resources.c screenshot-resources.h

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
	$()

gnome_screenshot_CFLAGS =		\
	$(SCREENSHOT_CFLAGS)		\
	$(XSHAPE_CFLAGS)		\
	$()

gnome_screenshot_LDADD =		\
	$(SCREENSHOT_LIBS)		\
	$(XSHAPE_LIBS)			\
	-lm				\
	$()

gnome_screenshotdir = $(datadir)/applications
gnome_screenshot_in_files = org.gnome.Screenshot.desktop.in
gnome_screenshot_DATA = $(gnome_screenshot_in_files:.desktop.in=.desktop)
org.gnome.Screenshot.desktop: $(gnome_screenshot_in_files)
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@
CLEANFILES += $(gnome_screenshot_DATA)
EXTRA_DIST += $(gnome_screenshot_in_files)

appdatadir = $(datadir)/metainfo
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = org.gnome.Screenshot.metainfo.xml.in
org.gnome.Screenshot.metainfo.xml: $(appdata_in_files)
	$(AM_V_GEN)$(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@
CLEANFILES += $(appdata_DATA)
EXTRA_DIST += $(appdata_in_files)

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_in_files = org.gnome.Screenshot.service.in
dbusservice_DATA = $(dbusservice_in_files:.service.in=.service)
org.gnome.Screenshot.service: $(dbusservice_in_files)
	$(AM_V_GEN) $(SED) -e 's|\@bindir\@|@bindir@|' $< > $@
CLEANFILES += $(dbusservice_DATA)
EXTRA_DIST += $(dbusservice_in_files)

gsettings_SCHEMAS = org.gnome.gnome-screenshot.gschema.xml
EXTRA_DIST += $(gsettings_SCHEMAS)
@GSETTINGS_RULES@

convertdir = $(datadir)/GConf/gsettings
convert_DATA = gnome-screenshot.convert

man_MANS = gnome-screenshot.1
