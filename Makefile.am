## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS  = -I m4
AUTOMAKE_OPTIONS = foreign
dist_doc_DATA    = COPYING README.md
AM_CFLAGS        = -Wall $(AX_CFLAGS)

## --------------------------------------------------------------------

EXTRA_DIST            = libringbuf.pc.in
pkgconfigdir          = $(libdir)/pkgconfig
nodist_pkgconfig_DATA = libringbuf.pc

## --------------------------------------------------------------------

#### libraries

include_HEADERS = ringbuf.h

lib_LTLIBRARIES  = libringbuf.la
libringbuf_la_LDFLAGS = -version-info 0:0:0
libringbuf_la_SOURCES = \
	ringbuf.c

#page
#### test

check_PROGRAMS = \
	ringbuf-test

TESTS = $(check_PROGRAMS)

ringbuf_test_LDADD      = libringbuf.la

installcheck-local: $(TEST)
	@for f in $(TEST); do $(builddir)/$$f; done

### end of file
