null_outltlibs = libnull_out.la
null_outldflags = -export-dynamic -avoid-version
null_outsources = null.c

lib_LTLIBRARIES = $(null_outltlibs)

AM_CFLAGS = @DFLAGS@ -D_REENTRANT -I$(top_builddir) \
		-I$(top_srcdir)/libalsaplayer -I$(top_srcdir)/alsaplayer

libdir = $(ADDON_DIR)/output

libnull_out_la_LDFLAGS = $(null_outldflags)
LIBS =

libnull_out_la_SOURCES= $(null_outsources)
