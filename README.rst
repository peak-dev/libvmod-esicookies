===============
vmod_esicookies
===============

-------------------------------------------
Varnish Module for cookie handling with ESI
-------------------------------------------

:Author: Nils Goroll
:Date: 2014-10-16
:Version: 1.1
:Manual section: 3

.. _synopsis:

SYNOPSIS
========

::

	import esicookies;

	sub vcl_fetch {
		esicookies.to_esi(beresp.http.Set-Cookie);
	}

Reporting of errors and warnings:

::

	import esicookies;

	sub vcl_fetch {
		set req.http.X-Err = esicookies.to_esi_e(beresp.http.Set-Cookie);
		if (req.http.X-Err && req.http.X-Err != "") {
			error 503 "Error in to_esi";
		}
		set req.http.X-Warn = esicookies.warnings();
		if (req.http.X-Warn == "") {
			unset req.http.X-Warn;
		}
	}

	sub vcl_error {
		if (req.http.X-Err) {
			set obj.http.X-Err = req.http.X-Err;
		}
	}

NOTE ON UPGRADING
=================

When upgrading from versions before 1.1, please see the history_ for
important changes!

DESCRIPTION
===========

This module's special purpose is to add HTTP cookies from ``Set-Cookie``
headers of backend responses to the ``http0`` context which is used as
a template for subsequent ESI requests and rollbacks.

The net effect is that backends can set cookies which will be seen by
other backends for subsequent ESI requests as if the individual ESI
responses' ``Set-Cookie`` headers had reached the browser.

FUNCTIONS
=========

See see synopsis_ for a typical (and the only tested) usage example.

.. _toesi:

to_esi
--------

Prototype
	::

		esicookies.to_esi(HEADER);


When the ``to_esi`` function is called, all instances of the named
``Set-Cookie`` reponse header and the original request's ``Cookie``
headers are parsed and a new ``Cookie`` header is generated in the
``http0`` context, which will be used for subsequent ESI requests and
after a rollback.

Later ``Set-Cookie`` reponse headers overwrite Cookies present in the
initial ``http0`` context ``Cookie`` headers or earlier ``Set-Cookie``
reponse headers.

Parse warnings are logged to VSM and can also be queried from VCL
using the warnings_ function.

For VSM logging, the ``VCL_error`` tag is used (because there is no
tag for warnings). Log entries contain information about Cookie
elements being `tolerated` or `skipped` and a hint on where the parse
warning occurred. The excerpt is limited to 40 characters from the
Cookie line, if necessary. Sample output:

::

	13 VCL_error    c vmod esicookies cookies tolerated in hdr:
	13 VCL_error    c ...ngcookieline;ok=val;noval=;ok2=val;somuc...
	13 VCL_error    c                        ^- empty cookie value



to_esi_e
----------

Prototype
	::

		set ... = esicookies.to_esi_e(HEADER);
		if (esicookies.to_esi_e(HEADER))


This form is semantically equivalent to to_esi_ except that is
returns a string when an error is encountered.

Possible return strings are:

* ``exceeded number of allowed cookies``: too many cookies in use (see
  limitations_)
* ``new cookies: not even the header name fits`` and ``new cookies
  dont fit``: Cookies don't fit into the workspace of size
  ``HTTP0_WS_SIZE`` (see limitations_)

.. _warnings:

warnings
--------

Prototype
	::

		set ... = esicookies.warnings();

Returns a summary of parse warnings which have been encountered and
logged to VSM.

Possible return strings are:

* ``cookies skipped``: Some Cookie header elements were skipped while
  parsing (and are thus missing from the generated ``Cookie:`` header
  for subsequent ESI requests).
* ``cookies tolerated``: Some Cookie header elements were not properly
  formatted (e.g. contained no value), but were processed anyway.
* ``cookies skipped and tolerated``: Both of the above

.. _tohttp0:

to_http0
--------

Prototype
	::

		esicookies.to_http0(HEADER);

DEPRECATED. This function is an alias for ``to_esi()``, and is included
for backwards compatibility with version 1.0. It will be removed in
future versions.

.. _tohttp0e:

to_http0_e
----------

Prototype
	::

		set ... = esicookies.to_esi_e(HEADER);

DEPRECATED. This function is an alias for ``to_esi_e()``, and is included
for backwards compatibility with version 1.0. It will be removed in
future versions.

.. _limitations:

LIMITATIONS
===========

Two compile-time defines limit the number and total size of all
cookies:

* ``HTTP0_WS_SIZE``: workspace for new Cookie Headers, defaults to 4
  KB

* ``max_cookies``: Maximum number of cookies, defaults to 180

Other limitations:

* Any attributes in ``Set-Cookie`` response headers but ``Expires``
  and ``max-age`` are currently ignored.

* The ``Set-Cookie`` attribues ``Expires`` and ``max-age`` are only
  evaluated to determine if Cookies should be deleted due to the
  respective date being in the past at the time a ``Set-Cookie`` is
  processed.
  Otherwise Cookies are assumed not to expire between the time of the
  ``Set-Cookie`` response header being processed and the ``Cookie``
  header being generated.

* The Name of the ``Cookie`` header cannot currently be changed.

INSTALLATION
============

The source distribution uses autotools to configure the build, and
does also have the necessary bits in place to do functional unit tests
using the varnishtest tool.

Usage::

 ./configure VARNISHSRC=DIR [VMODDIR=DIR]

`VARNISHSRC` is the directory of the Varnish source tree for which to
compile your vmod. **On Linux, Varnish should be compiled against a current,
system-installed libjemalloc** (see known_issues_).

Optionally you can also set the vmod install directory by adding
`VMODDIR=DIR` (defaults to the pkg-config discovered directory from your
Varnish installation).

Make targets:

* ``make`` - builds the vmod
* ``make install`` - installs your vmod in `VMODDIR`
* ``make check`` - runs the unit tests in ``src/tests/*.vtc``

Running ``make check`` is strongly recommended.

.. _known_issues:

KNOWN ISSUES
============

* On Linux, if ``make check`` fails for `vmod_esicookies_reload.vtc`,
  inspect the error log. If it reports a segmentation violation
  (SIGSEGV) in varnishd, your varnish sources have most likely been
  compiled with the (outdated) bundled jemalloc.

  To avoid this issue, either

  1. compile varnish ``--without-jemalloc``

  2. or make sure that an up-to-date `jemalloc` development package is
     installed on your system (probably called `libjemalloc-dev` or
     `jemalloc-devel`) and re-build Varnish. Check the `config.log`
     for a `No system jemalloc found` warning and re-iterate if this
     warning is found.

* Varnish 3 releases differ in their behaviour with regard to empty
  headers. Setting a header to the result of the to_esi_e_ and
  warnings_ functions may produce a header with no value.

  To ensure compatibility with all Varnish 3 releases, always use the
  checks for the empty header as in the examples given herein.

.. _history:

HISTORY / CHANGELOG
===================

* Version 1.0: Initial version.

* Version 1.1: Initial version.

  * to_esi_e_ now returns NULL when there was no error.

  * changed strings returned by to_esi_e_

  * Added the warnings_ function and VSM logging for parse warnings.

  * The parser is now more tolarant

  * replace `to_http0` with `to_esi` and `to_http0_e` with `to_esi_e`

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-esicookies project. See LICENSE for details.

Copyright (c) 2013-2014 UPLEX Nils Goroll Systemoptimierung. All rights
reserved.
