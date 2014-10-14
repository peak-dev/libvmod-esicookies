===============
vmod_esicookies
===============

-------------------------------------------
Varnish Module for cookie handling with ESI
-------------------------------------------

:Author: Nils Goroll
:Date: 2014-10-14
:Version: 1.1
:Manual section: 3

.. _synopsis:

SYNOPSIS
========

::

	import esicookies;

	sub vcl_fetch {
		esicookies.to_http0(beresp.http.Set-Cookie);
	}

	# OR

	sub vcl_fetch {
		set req.http.X-Err = esicookies.to_http0_e(beresp.http.Set-Cookie);
		if (req.http.X-Err) {
			error 503 "Error in to_http0";
		}
		set req.http.X-Warn = esicookies.warnings();
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

.. _tohttp0:

to_http0
--------

Prototype
	::

		esicookies.to_http0(HEADER);


The ``http0`` context contains a copy of the original request headers
as requested by the client.

When the ``to_http0`` function is called, all instances of the named
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
tag for warnings). Log entries contain formation about Cookie
elements being `tolerated` or `skipped` and a hint on where the parse
warning occurred. The excerpt is limited to 40 characters from the
Cookie line, if necessary. Sample output:

::

	13 VCL_error    c vmod esicookies http0 cookies tolerated in hdr:
	13 VCL_error    c ...ngcookieline;ok=val;noval=;ok2=val;somuc...
	13 VCL_error    c                        ^- empty cookie value



to_http0_e
----------

Prototype
	::

		set ... = esicookies.to_http0_e(HEADER);
		if (esicookies.to_http0_e(HEADER))


This form is semantically equivalent to to_http0_ except that is
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

.. _limitations:

LIMITATIONS
===========

Two compile-time defines limit the number and total size of all
cookies:

* ``HTTP0_WS_SIZE``: workspace for new Cookie Headers, defaults to 4
  KB

* ``max_cookies``: Maximum number of cookies, defaults to 180

* Attributes in ``Set-Cookie`` response headers like ``Expires``,
  ``Domain`` or ``Path`` are currently ignored.

* The Name of the ``Cookie`` header cannot currently be changed.

INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the varnishtest tool.

Usage::

 ./configure VARNISHSRC=DIR [VMODDIR=DIR]

`VARNISHSRC` is the directory of the Varnish source tree for which to
compile your vmod.

Optionally you can also set the vmod install directory by adding
`VMODDIR=DIR` (defaults to the pkg-config discovered directory from your
Varnish installation).

Make targets:

* make - builds the vmod
* make install - installs your vmod in `VMODDIR`
* make check - runs the unit tests in ``src/tests/*.vtc``

CHANGES

.. _history:

HISTORY / CHANGELOG
===================

* Version 1.0: Initial version.

* Version 1.1: Initial version.

  * to_http0_e_ now returns NULL when there was no error, contrary
    to the empty string as before. This change is to avoid production
    of invalid HTTP headers (without a value) when `to_http0_e_` is
    used as in the examples shown.

    Thus, to check for errors in VCL, if ``(... != "")`` needs to be
    replaced with if ``(...)``.

  * changed strings returned by to_http0_e_

  * Added the warnings_ function and VSM logging for parse warnings.

  * The parser is now more tolarant

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-esicookies project. See LICENSE for details.

Copyright (c) 2013-2014 UPLEX Nils Goroll Systemoptimierung. All rights
reserved.
