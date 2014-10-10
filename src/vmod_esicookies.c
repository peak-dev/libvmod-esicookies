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

#include <ctype.h>
#define __EXTENSIONS__
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <sys/resource.h>
#include "vrt.h"
#include "cache.h"
#include "vcc_if.h"
#include "vas.h"

/* ----------------------------------------------------------------------
 * static functions copied from other varnish code.
 * code will break when these change
 * ----------------------------------------------------------------------
 */

/* XXX cache_vrt.c should really expose these */
static struct http *
vrt_selecthttp(const struct sess *sp, enum gethdr_e where)
{
	struct http *hp;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	switch (where) {
	case HDR_REQ:
		hp = sp->http;
		break;
	case HDR_BEREQ:
		hp = sp->wrk->bereq;
		break;
	case HDR_BERESP:
		hp = sp->wrk->beresp;
		break;
	case HDR_RESP:
		hp = sp->wrk->resp;
		break;
	case HDR_OBJ:
		CHECK_OBJ_NOTNULL(sp->obj, OBJECT_MAGIC);
		hp = sp->obj->http;
		break;
	default:
		INCOMPL();
	}
	CHECK_OBJ_NOTNULL(hp, HTTP_MAGIC);
	return (hp);
}

static int
http_IsHdr(const txt *hh, const char *hdr)
{
	unsigned l;

	Tcheck(*hh);
	AN(hdr);
	l = hdr[0];
	assert(l == strlen(hdr + 1));
	assert(hdr[l] == ':');
	hdr++;
	return (!strncasecmp(hdr, hh->b, l));
}

/* ----------------------------------------------------------------------
 * our own per-session workspaces for modifications of the http0 object.
 *
 * as long as we are sure to copy the Cookie header every time, we can always
 * copy from ws[0] to ws[1]
 *
 */

struct http0_mem {
	unsigned		magic;
#define VMOD_HTTP0_MEM_MAGIC	0x6874306d
	struct ws		ws[2];
	unsigned		xid;
	unsigned short		next_ws;
};

#define HTTP0_WS_SIZE (4*1024)

struct http0_meta {
	unsigned		magic;
#define VMOD_HTTP0_META_MAGIC	0x68746d6d
	struct http0_mem	*mem;
	unsigned		nmem;
};

static void
http0_free(void *ptr) {
	struct http0_meta	*meta = ptr;
	struct http0_mem	*m;
	int			i;

	if (! meta)
		return;

	CHECK_OBJ_NOTNULL(meta, VMOD_HTTP0_META_MAGIC);

	if (meta->nmem) {
		for (i = 0; i < meta->nmem; i++) {
			m = &(meta->mem[i]);
			CHECK_OBJ_NOTNULL(m, VMOD_HTTP0_MEM_MAGIC);
			if (m->ws[0].s) {
				WS_Assert(&m->ws[0]);
				WS_Assert(&m->ws[1]);
				// only the first ws start ptr alloc'ed
				free (m->ws[0].s);
				m->magic = 0;
			}
		}
		free(meta->mem);
	}

	free(meta);
}

int
init_function(struct vmod_priv *priv, const struct VCL_conf *cfg)
{
	struct http0_meta	*meta;
	struct rlimit		nofile, rltest;
	int			i;

	(void)cfg;

	AZ(getrlimit(RLIMIT_NOFILE, &nofile));

	/* make sure we can't raise our limit, otherwise we can't
	   assume that nmem be bound by rlim_max */
	rltest.rlim_cur = nofile.rlim_cur;
	rltest.rlim_max = nofile.rlim_max + 1;
	AN(setrlimit(RLIMIT_NOFILE, &rltest));
	assert(errno == EPERM);

	meta = malloc(sizeof(*meta));
	XXXAN(meta);
	meta->magic = VMOD_HTTP0_META_MAGIC;
	meta->nmem  = nofile.rlim_max;

	meta->mem   = calloc(meta->nmem, sizeof(struct http0_mem));
	XXXAN(meta->mem);
	for (i = 0; i < meta->nmem; i++)
		meta->mem[i].magic = VMOD_HTTP0_MEM_MAGIC;

	priv->priv = meta;
	priv->free = http0_free;
	return (0);
}

static void
http0_mem_ws_alloc(struct http0_mem *m) {
	char	*space;

	AZ(m->ws[0].s);
	AZ(m->ws[1].s);

	space = valloc(2 * HTTP0_WS_SIZE);

	AN(space);

	WS_Init(&m->ws[0], "http0 ws[0]",  space,
	    HTTP0_WS_SIZE);
	WS_Init(&m->ws[1], "http0 ws[1]", (space + HTTP0_WS_SIZE),
	    HTTP0_WS_SIZE);
}

static struct ws *
http0_get_mem(struct sess *sp, struct http0_meta *meta) {
	struct ws		*ws;
	struct http0_mem	*m;

	assert(sp->id < meta->nmem);
	m = &meta->mem[sp->id];
	CHECK_OBJ_NOTNULL(m, VMOD_HTTP0_MEM_MAGIC);

	if (m->ws[0].s) {
		if (m->xid != sp->xid) {
			m->xid = sp->xid;
		}

		ws = &m->ws[m->next_ws];
		m->next_ws = m->next_ws ? 0 : 1;
		WS_Reset(ws, NULL);
		return (ws);
	}

	http0_mem_ws_alloc(m);
	AZ(m->next_ws);
	ws = &m->ws[0];
	m->next_ws = 1;
	return (ws);
};

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

static struct cookie *
vesico_cookie_lookup(struct cookiehead *cookies, const txt name) {
	struct cookie	*c;
	int l;

	l = Tlen(name);
	assert(l);

	VSTAILQ_FOREACH(c, cookies, list) {
		if (! c->valid)
			continue;
		if (Tlen(c->name) != l)
			continue;
		if (strncmp(c->name.b, name.b, l) == 0)
			return c;
	}

	return NULL;
}

static int
vesico_analyze_cookie_header(struct sess *sp, const txt hdr,
		      struct cookiehead *cookies, struct cookies *cs) {
	char	*p = hdr.b;
	char	*pp;

	while (p) {
		struct cookie	*c, *c2;

		if (cs->used >= max_cookies)
			return EOVERFLOW;

		c = &cs->space[cs->used++];

		c->valid = 0;

		while (isspace(*p))
			p++;
		c->name.b = p;

		p = strchr(p, '=');
		if (! p)
			goto cookie_invalid;

		pp = p - 1;
		while (isspace(*pp))
			pp--;
		c->name.e = pp + 1;
		if (c->name.b >= c->name.e)
			goto cookie_invalid;

		p++;
		while (isspace(*p))
			p++;
		c->value.b = p;

		p = strchr(p, ';');
		if (p && p < hdr.e) {
			pp = p - 1;
			while (isspace(*pp))
				pp--;
			pp++;
			if (pp <= c->value.b)
				goto cookie_invalid;
			c->value.e = pp;

			// skip forward to next cookie
			p++;
			while (isspace(*p))
				p++;
		} else {
			pp = hdr.e - 1;
			while (isspace(*pp))
				pp--;
			pp++;
			if (pp <= c->value.b)
				goto cookie_invalid;
			c->value.e = pp;

			p = NULL;
		}

		if (! Tlen(c->name))
			goto cookie_invalid;

		if (! Tlen(c->value))
			goto cookie_invalid;

		/* check if seen before and, if yes, make that one invalid */
		c2 = vesico_cookie_lookup(cookies, c->name);
		if (c2)
			c2->valid = 0;

		c->valid = 1;
		VSTAILQ_INSERT_TAIL(cookies, c, list);
		continue;

	  cookie_invalid:
		WSP(sp, SLT_VCL_error,
		    "vmod esicookies http0: invalid header '%s'", hdr.b);
		cs->used--;
		return EINVAL;
	}
	return 0;
}

/*
 * Add Cookies from the given Set-Cookie header (which can have an arbitrary
 * name as passed with the only argument, but need to confirm with the basic
 * Set-Cookie format) to the first Cookie header of the http0
 *
 * TODO:
 * - check cookie attributes ?
 *   - expires?
 *   - domain?
 *   - path?
 *
 * returns error or empty string
 */

const char *H_COOKIE = "\007Cookie:";

static const char *
vesico_write_cookie_hdr(struct sess *sp, struct http0_meta *meta,
			struct http *h0, struct cookiehead *cookies)
{
	unsigned	u;
	char *b = NULL, *e = NULL;
	struct cookie	*c;
	struct ws	*ws = http0_get_mem(sp, meta);

	u = WS_Reserve(ws, 0);
	b = ws->f;
	e = b + u;

	if (u < (*H_COOKIE + 1)) {
		WS_Release(ws, 0);
		return "new cookies: not even the header name fits";
	}
	memcpy(b, H_COOKIE + 1, *H_COOKIE);
	b   += *H_COOKIE;
	*b++ = ' ';

	VSTAILQ_FOREACH(c, cookies, list) {
		if (! c->valid)
			continue;

		// data we read must not be from the ws we write to
		assert(! ((c->name.b >= ws->s) && (c->name.e <= ws->e)));

		unsigned l = (unsigned)(c->value.e - c->name.b);
		// exclude whitespace from check
		if (! (b + l + 2 < e)) {
			WS_Release(ws, 0);
			return "new cookies dont fit";
		};

		l = Tlen(c->name);
		memcpy(b, c->name.b, l);
		b += l;

		*b++ = '=';

		l = Tlen(c->value);
		memcpy(b, c->value.b, l);
		b += l;

		b[0] = ';'; b[1] = ' ';
		b += 2;
	}
	b -= 2;
	*b++ = '\0';

	http_Unset(h0, H_COOKIE);
	http_SetHeader(sp->wrk, sp->fd, h0, ws->f);

	WS_ReleaseP(ws, b);

	return "";
}

/*
 * XXX TODO optimize for the case where existing cookie is tail of CS and we
 * don't change existing cookies ?
 */

static const char *
vesico_to_http0(struct sess *sp, struct vmod_priv *priv, enum gethdr_e where,
		const char *hdr)
{
	struct http0_meta	*meta = priv->priv;

	struct cookiehead	cookies =
	    VSTAILQ_HEAD_INITIALIZER(cookies);
	struct cookies	cs;

	cs.used = 0;

	struct http		*hp, *h0;
	unsigned		n;
	int			used;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	h0 = sp->http0;
	CHECK_OBJ_NOTNULL(h0, HTTP_MAGIC);

	CHECK_OBJ_NOTNULL(meta, VMOD_HTTP0_META_MAGIC);

	/* collect existing cookies */
	for (n = HTTP_HDR_FIRST; n < h0->nhd; n++) {
		if (http_IsHdr(&h0->hd[n], H_COOKIE)) {
			int			ret;
			txt			h;

			Tcheck(h0->hd[n]);
			h.b = h0->hd[n].b + *H_COOKIE;
			while (isspace(*(h.b)))
				h.b++;
			h.e = h0->hd[n].e;

			ret = vesico_analyze_cookie_header(sp, h, &cookies,
			    &cs);
			if (ret) {
				return strerror(ret);
			}
		}
	}

	used = cs.used;

	/* collect cookies from the set-cookie hdr given */
	hp = vrt_selecthttp(sp, where);
	for (n = HTTP_HDR_FIRST; n < hp->nhd; n++) {
		if (http_IsHdr(&hp->hd[n], hdr)) {
			int			ret;
			txt			h;
			char			*p;

			Tcheck(hp->hd[n]);
			h.b = hp->hd[n].b + *hdr;
			while (isspace(*(h.b)))
				h.b++;

			p = strchr(h.b, ';');
			if (p)
				h.e = p;
			else
				h.e = hp->hd[n].e;

			ret = vesico_analyze_cookie_header(sp, h, &cookies,
			    &cs);
			if (ret) {
				return strerror(ret);
			}
		}
	}

	/* if we haven't used any more cookies than we already had we're done */
	if (used == cs.used)
		return "";

	return (vesico_write_cookie_hdr(sp, meta, h0, &cookies));
}

const char * __match_proto__()
vmod_to_http0_e(struct sess *sp, struct vmod_priv *priv, enum gethdr_e where,
		const char *hdr) {
	return (vesico_to_http0(sp, priv, where, hdr));
}

void __match_proto__()
vmod_to_http0(struct sess *sp, struct vmod_priv *priv, enum gethdr_e where,
	      const char *hdr) {
	return (void)(vesico_to_http0(sp, priv, where, hdr));
}
