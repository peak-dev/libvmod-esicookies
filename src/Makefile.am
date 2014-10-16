AM_CPPFLAGS = \
	-I$(VARNISHSRC)/include \
	-I$(VARNISHSRC)/bin/varnishd

AM_LDFLAGS  = $(AM_LT_LDFLAGS)

vmoddir = $(VMODDIR)
vmod_LTLIBRARIES = libvmod_esicookies.la
vmod_srcdir = $(top_srcdir)/src
vmodtool = $(VARNISHSRC)/lib/libvmod_std/vmod.py

libvmod_esicookies_la_LDFLAGS = $(AM_LDFLAGS) \
	-module -export-dynamic -avoid-version -shared

libvmod_esicookies_la_SOURCES = \
	vmod_esicookies.c	\
	vmod_esicookies.h

nodist_libvmod_esicookies_la_SOURCES = \
	vcc_if.c \
	vcc_if.h

# BUILT_SOURCES is only a hack and dependency tracking does not help
# for the first build

vmod_esicookies.lo: vcc_if.h

vcc_if.c vcc_if.h: $(vmodtool) $(vmod_srcdir)/vmod_esicookies.vcc
	@PYTHON@ $(vmodtool) $(vmod_srcdir)/vmod_esicookies.vcc

VMOD_TESTS = tests/*.vtc


check: $(VARNISHSRC)/bin/varnishtest/varnishtest
	$(VARNISHSRC)/bin/varnishtest/varnishtest \
		-Dvarnishd=$(VARNISHSRC)/bin/varnishd/varnishd \
		-Dvmod_topbuild=$(abs_top_builddir) \
		tests/*.vtc

EXTRA_DIST = \
	vmod_esicookies.vcc	\
	$(VMOD_TESTS)

CLEANFILES = $(builddir)/vcc_if.c $(builddir)/vcc_if.h
