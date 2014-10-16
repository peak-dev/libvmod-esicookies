/*-
 * Copyright (c) 2013-2014 UPLEX Nils Goroll Systemoptimierung
 * All rights reserved
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "config.h"
#include "cache.h"


/* ----------------------------------------------------------------------
 * our own per-session workspaces for modifications of the http0 object.
 *
 * as long as we are sure to copy the Cookie header every time, we can always
 * copy from ws[0] to ws[1]
 *
 */

struct vesico_req {
	unsigned		magic;
#define VESICO_REQ_MAGIC	0x6874306d

	struct ws		ws[2];
	unsigned		xid;
	unsigned short		next_ws;

	unsigned		warn;
};

#define VESICO_WS_SIZE (4*1024)

struct vesico_meta {
	unsigned		magic;
#define VESICO_META_MAGIC	0x68746d6d
	struct vesico_req	*mem;
	unsigned		nmem;
};

/* return values */
#define VESICO_OK		0
#define VESICO_ERR_OVERFLOW	(1<<0)
#define VESICO_ERR_LIM		(1<<1)

/* warn member of vesico_req */
#define VESICO_WARN_SKIPPED	(1<<0)
#define VESICO_WARN_TOLERATED	(1<<1)
#define VESICO_WARN_LIM	(1<<2)

struct cookie {
	VSTAILQ_ENTRY(cookie)	list;
	txt			name;
	txt			value;
	int			valid;
};
VSTAILQ_HEAD(cookiehead, cookie);

/*
 * http://webdesign.about.com/od/cookies/f/cookies-per-domain-limit.htm
 *  Chrome 9 allowed 180 cookies per domain
 */
#define max_cookies	180

struct cookies {
	struct cookie		space[max_cookies];
	int			used;
};

enum vesico_analyze_action {
	VESICOAC_DEL = 0,
	VESICOAC_ADD,
	_VESICOAC_LIM
};

