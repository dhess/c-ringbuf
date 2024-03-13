dnl @configure_input@
dnl

AC_PREREQ([2.72])
AC_INIT([c-ringbuf],[0.0.1],[],[ringbuf],[http://github.com/dhess/c-ringbuf])

AC_CONFIG_MACRO_DIRS([m4])
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE([foreign dist-xz no-dist-gzip])
AM_MAINTAINER_MODE

AM_PROG_AR
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_MKDIR_P

LT_PREREQ([2.4])
LT_INIT

dnl This is the version stored in the pkg-config data file.
AC_SUBST([LIBRINGBUF_PKG_CONFIG_VERSION],[0.0.1])

dnl page
#### basic system inspection

AC_LANG([C])
AM_PROG_AS
AM_PROG_CC_C_O

AC_CACHE_SAVE

dnl page
#### finish

AC_CONFIG_FILES([Makefile]
   [libringbuf.pc])
AC_OUTPUT

### end of file
# Local Variables:
# mode: autoconf
# page-delimiter: "^dnl page"
# End:
