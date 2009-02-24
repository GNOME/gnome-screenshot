NULL =

INCLUDES =							\
	-I.							\
	-I$(srcdir)						\
	-DGNOMELOCALEDIR=\""$(prefix)/$(DATADIRNAME)/locale"\"	\
	-DUIDIR=\""$(uidir)"\"					\
	$(GNOME_UTILS_CFLAGS)					\
	$(GTHREAD_CFLAGS)					\
	$(NULL)

bin_PROGRAMS = \
	gnome-screenshot		\
	$(NULL)

gnome_screenshot_SOURCES = \
	gnome-screenshot.c		\
	screenshot-dialog.c		\
	screenshot-dialog.h		\
	screenshot-shadow.c		\
	screenshot-shadow.h		\
	screenshot-utils.c		\
	screenshot-utils.h		\
	screenshot-save.c		\
	screenshot-save.h		\
	screenshot-xfer.c		\
	screenshot-xfer.h		\
	$(NULL)

gnome_screenshot_LDFLAGS = -export-dynamic

gnome_screenshot_LDADD =		\
	$(XSHAPE_LIBS)			\
	$(GNOME_UTILS_LIBS)		\
	$(GTHREAD_LIBS)			\
	$(NULL)

gnome_screenshotdir  = $(datadir)/applications
gnome_screenshot_in_files = gnome-screenshot.desktop.in
gnome_screenshot_DATA = $(gnome_screenshot_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

uidir = $(datadir)/gnome-screenshot
ui_DATA =					\
	gnome-screenshot.ui			\
	$(NULL)

EXTRA_DIST =					\
	$(schemas_in_files)			\
	$(gnome_screenshot_in_files)		\
	$(ui_DATA)				\
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
