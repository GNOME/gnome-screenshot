NULL =

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DUIDIR=\""$(uidir)"\"					\
	$(NULL)

bin_PROGRAMS = \
	gnome-screenshot		\
	$(NULL)

gnome_screenshot_SOURCES = \
	gnome-screenshot.c		\
	gnome-screenshot.h		\
	cheese-flash.c			\
	cheese-flash.h			\
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
	$(GLIB_CFLAGS)			\
	$(GIO_CFLAGS)			\
	$(LIBCANBERRA_GTK_CFLAGS)	\
	$(GTHREAD_CFLAGS)		\
	$(GTK_CFLAGS)			\
	$(NULL)

gnome_screenshot_LDFLAGS = -export-dynamic

gnome_screenshot_LDADD =		\
	$(XSHAPE_LIBS)			\
	$(GLIB_LIBS)			\
	$(GIO_LIBS)			\
	$(LIBCANBERRA_GTK_LIBS)		\
	$(GTHREAD_LIBS)			\
	$(GTK_LIBS)			\
	-lm				\
	$(NULL)

gnome_screenshotdir  = $(datadir)/applications
gnome_screenshot_in_files = gnome-screenshot.desktop.in
gnome_screenshot_DATA = $(gnome_screenshot_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

man_MANS = gnome-screenshot.1

uidir = $(datadir)/gnome-screenshot
ui_DATA =					\
	gnome-screenshot.ui			\
	$(NULL)

gsettingsschema_in_files = org.gnome.gnome-screenshot.gschema.xml.in
gsettings_SCHEMAS = $(gsettingsschema_in_files:.xml.in=.xml)
.PRECIOUS: $(gsettings_SCHEMAS)

convertdir = $(datadir)/GConf/gsettings
convert_DATA = gnome-screenshot.convert

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

EXTRA_DIST =					\
	$(gsettingsschema_in_files)		\
	$(gnome_screenshot_in_files)		\
	$(man_MANS)				\
	$(ui_DATA)				\
	$(convert_DATA)				\
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES)			 \
	$(gsettings_SCHEMAS)			 \
	$(gnome_screenshot_DATA)		 \
	$(sys_DATA)

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

install-exec-local:
	rm -f $(DESTDIR)$(bindir)/gnome-panel-screenshot
	ln -s gnome-screenshot $(DESTDIR)$(bindir)/gnome-panel-screenshot
