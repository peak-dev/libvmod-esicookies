===============
vmod_esicookies
===============

-------------------------------------------
Varnish Module for cookie handling with ESI
-------------------------------------------

:Author: Nils Goroll
:Date: 2013-04-21
:Version: 1.0
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
		if (req.http.X-Err != "") {
			error 503 "Error in to_http0";
		}
		unset req.http.X-Err;
	}

	sub vcl_error {
		if (req.http.X-Err) {
			set obj.http.X-Err = req.http.X-Err;
		}
	}


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

to_http0_e
----------

Prototype
	::

		set ... = esicookies.to_http0_e(HEADER);
		if (esicookies.to_http0_e(HEADER) ...)


This form is semantically equivalent to tohttp0_ except that is
returns a non-empty string when an error is encountered.

Possible return strings are:

* "Value too large for defined data type" or your current locale's
  translation for ``EOVERFLOW``: too many cookies in use (see
  limitations_)
* "Invalid argument" or your current locale's translation for
  ``EINVAL``: a Cookie or Set-Cookie header had an illegal syntax
* "new cookies: not even the header name fits"
* "new cookies dont fit": Cookies don't fit into the workspace of size
  ``HTTP0_WS_SIZE`` (see limitations_)


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


HISTORY
=======

Version 1.0: Initial version.

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-esicookies project. See LICENSE for details.

Copyright (c) 2013 UPLEX Nils Goroll Systemoptimierung. All rights
reserved.
