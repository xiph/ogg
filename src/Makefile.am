## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/include

lib_LTLIBRARIES = libogg.la

libogg_la_SOURCES = framing.c bitwise.c
libogg_la_LDFLAGS = -version-info 1:0:0

debug:
	$(MAKE) all CFLAGS="@DEBUG@"

profile:
	$(MAKE) all CFLAGS="@PROFILE@"
