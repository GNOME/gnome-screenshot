NULL =

# Used by the GNOME_PROGRAM_STANDARD_PROPERTIES macros
STANDARD_PROPERTIES_CFLAGS = 					\
	-DPREFIX=\""$(prefix)"\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\"			\
	-DLIBDIR=\""$(libdir)"\"				\
	-DDATADIR=\""$(datadir)"\"				\
	$(NULL)

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	$(STANDARD_PROPERTIES_CFLAGS)				\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DGLADEDIR=\""$(gladedir)"\"				\
	$(SCREENSHOT_CFLAGS)					\
	$(NULL)

bin_PROGRAMS = \
	gnome-screenshot		\
	$(NULL)

gnome_screenshot_SOURCES = \
	gnome-panel-screenshot.c	\
	gnome-panel-screenshot.glade    \
	screenshot-dialog.c		\
	screenshot-dialog.h		\
	screenshot-shadow.c		\
	screenshot-shadow.h		\
	screenshot-utils.c		\
	screenshot-utils.h		\
	gnome-egg-xfer-dialog.c		\
	gnome-egg-xfer-dialog.h		\
	gnome-egg-xfer-dialog-icons.h	\
	screenshot-save.c		\
	screenshot-save.h		\
	screenshot-xfer.c		\
	screenshot-xfer.h		\
	$(NULL)

gnome_screenshot_LDFLAGS = -export-dynamic

gnome_screenshot_LDADD =		\
	$(XSHAPE_LIBS)			\
	$(SCREENSHOT_LIBS)		\
	$(NULL)

gnome_screenshotdir  = $(datadir)/applications
gnome_screenshot_in_files = gnome-screenshot.desktop.in
gnome_screenshot_DATA = $(gnome_screenshot_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

gladedir = $(datadir)/gnome-screenshot/glade

glade_DATA =					\
	gnome-screenshot.glade			\
	$(NULL)

EXTRA_DIST =					\
	$(glade_DATA)				\
	$(schemas_in_files)			\
	$(gnome_screenshot_in_files)		\
	$(NULL)

schemasdir       = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files =				\
	gnome-screenshot.schemas.in		\
	$(NULL)
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/gnome-screenshot/$$p ; \
		done ; \
	fi
else
install-data-local:
endif

CLEANFILES = \
	$(BUILT_SOURCES)			 \
	$(schemas_in_files:.schemas.in=.schemas) \
	$(gnome_screenshot_DATA)		 \
	$(sys_DATA)

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)

install-exec-local:
	rm -f $(DESTDIR)$(bindir)/gnome-panel-screenshot
	ln -s gnome-screenshot $(DESTDIR)$(bindir)/gnome-panel-screenshot
