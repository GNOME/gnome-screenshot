dnl -*- mode: m4 -*-
AC_PREREQ(2.63)
AC_INIT([gnome-screenshot],[3.22.0],[],[gnome-screenshot])

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 dist-xz no-dist-gzip foreign])
AM_SILENT_RULES([yes])

GETTEXT_PACKAGE=AC_PACKAGE_NAME
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[The name of the gettext domain])
IT_PROG_INTLTOOL(0.50.2)

AC_PROG_CC
AM_PROG_CC_C_O

GLIB_GSETTINGS

GLIB_MIN_VERSION=2.35.1
GTK_MIN_VERSION=3.0.0

msg_xshape=no
PKG_CHECK_MODULES([XSHAPE],
                  [xext x11],
                  [
                    AC_CHECK_HEADERS([X11/extensions/shape.h],
                                     [
                                      XSHAPE_LIBS="-lXext -lX11"
                                      msg_xshape=yes
                                     ])
                  ])
AC_SUBST(XSHAPE_LIBS)

PKG_CHECK_MODULES(SCREENSHOT,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gtk+-3.0 >= $GTK_MIN_VERSION
                  libcanberra-gtk3)

AC_CONFIG_FILES([
Makefile
src/Makefile
po/Makefile.in
])

AC_OUTPUT

dnl ==========================================================================
echo "

        GNOME Screenshot $VERSION
        =========================

        prefix:    ${prefix}
        compiler:  ${CC}

        XShape support: ${msg_xshape}

        Now type 'make' to build $PACKAGE
"
