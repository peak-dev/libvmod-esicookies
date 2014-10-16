/*-
 * Copyright (c) 2013-2014 UPLEX Nils Goroll Systemoptimierung
 * All rights reserved
 *
 * Author: Nils Goroll <nils.goroll@uplex.de>
 *
 * Portions Copyright (c) 2014 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <sys/resource.h>
#include "vrt.h"
#include "vct.h"
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

#if (HAVE_DECL_HTTP_ISHDR != 1)
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
#endif

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

const char * const vesico_err_str[VESICO_ERR_LIM] = {
	[VESICO_OK] = "ok",
	[VESICO_ERR_OVERFLOW] =
	"exceeded number of allowed cookies"
};

/* warn member of vesico_req */
#define VESICO_WARN_SKIPPED	(1<<0)
#define VESICO_WARN_TOLERATED	(1<<1)
#define VESICO_WARN_LIM	(1<<2)

const char * const vesico_warn_str[VESICO_WARN_LIM] = {
	[VESICO_OK] = "ok",
	[VESICO_WARN_SKIPPED] =
	"cookies skipped",
	[VESICO_WARN_TOLERATED] =
	"cookies tolerated",
	[VESICO_WARN_TOLERATED|VESICO_WARN_SKIPPED] =
	"cookies skipped and tolerated"
};

static void
vesico_free(void *ptr) {
	struct vesico_meta	*meta = ptr;
	struct vesico_req	*m;
	int			i;

	if (! meta)
		return;

	CHECK_OBJ_NOTNULL(meta, VESICO_META_MAGIC);

	if (meta->nmem) {
		for (i = 0; i < meta->nmem; i++) {
			m = &(meta->mem[i]);
			CHECK_OBJ_NOTNULL(m, VESICO_REQ_MAGIC);
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
	struct vesico_meta	*meta;
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
	meta->magic = VESICO_META_MAGIC;
	meta->nmem  = nofile.rlim_max;

	meta->mem   = calloc(meta->nmem, sizeof(struct vesico_req));
	XXXAN(meta->mem);
	for (i = 0; i < meta->nmem; i++)
		meta->mem[i].magic = VESICO_REQ_MAGIC;

	priv->priv = meta;
	priv->free = vesico_free;
	return (0);
}

static void
vesico_req_ws_alloc(struct vesico_req *m) {
	char	*space;

	AZ(m->ws[0].s);
	AZ(m->ws[1].s);

	space = valloc(2 * VESICO_WS_SIZE);

	AN(space);

	WS_Init(&m->ws[0], "http0 ws[0]",  space,
	    VESICO_WS_SIZE);
	WS_Init(&m->ws[1], "http0 ws[1]", (space + VESICO_WS_SIZE),
	    VESICO_WS_SIZE);
}

static inline struct vesico_req *
vesico_req(struct sess *sp, struct vesico_meta *meta) {
	struct vesico_req	*m;

	CHECK_OBJ_NOTNULL(meta, VESICO_META_MAGIC);
	assert(sp->id < meta->nmem);
	m = &meta->mem[sp->id];
	CHECK_OBJ_NOTNULL(m, VESICO_REQ_MAGIC);

	return (m);
}

static inline void
vesico_clear_warn(struct vesico_req *m) {
	m->warn = VESICO_OK;
}

static inline void
vesico_add_warn(struct vesico_req *m, unsigned warn) {
	m->warn |= warn;
}

static inline unsigned
vesico_r_warn(struct vesico_req *m) {
	return (m->warn);
}


static struct ws *
vesico_get_mem(struct sess *sp, struct vesico_req *m) {
	struct ws		*ws;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CHECK_OBJ_NOTNULL(m, VESICO_REQ_MAGIC);

	if (m->ws[0].s) {
		if (m->xid != sp->xid) {
			m->xid = sp->xid;
		}

		ws = &m->ws[m->next_ws];
		m->next_ws = m->next_ws ? 0 : 1;
		WS_Reset(ws, NULL);
		return (ws);
	}

	vesico_req_ws_alloc(m);
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

/*
 * From cache_http.c - Original version written by phk
 *
 *-----------------------------------------------------------------------------
 * Split source string at any of the separators, return pointer to first
 * and last+1 char of substrings, with whitespace trimed at both ends.
 * If sep being an empty string is shorthand for VCT::SP
 * If stop is NULL, src is NUL terminated.
 *
 * modified to use txt as arguments and internally to be agnostic to the
 * constness of txt in various varnish versions
 */

static int
http_split(txt *where, const char *sep, txt *tok)
{
	txt p;

	AN(where);
	AN(where->b);
	AN(sep);
	AN(tok);

	if (where->e == NULL)
	    where->e = strchr(where->b, '\0');

	for (p.b = where->b;
	     p.b < where->e && (vct_issp(*p.b) || strchr(sep, *p.b));
	     p.b++)
		continue;

	if (p.b >= where->e) {
		tok->b = NULL;
		tok->e = NULL;
		return (0);
	}

	tok->b = p.b;
	if (*sep == '\0') {
		for (p.e = p.b + 1; p.e < where->e && !vct_issp(*p.e); p.e++)
			continue;
		tok->e = p.e;
		where->b = p.e;
		return (1);
	}
	for (p.e = p.b + 1; p.e < where->e && !strchr(sep, *p.e); p.e++)
		continue;
	where->b = p.e;
	while (p.e > p.b && vct_issp(p.e[-1]))
		p.e--;
	tok->e = p.e;
	return (1);
}

/*
 * split a name <sep> value element
 *
 * return 0 if no seperator found (or only whitespace in where)
 * 1 otherwise
 *
 * name/value contain a NULL pointer if not found
 */
static int
http_nv(const txt where, const char *sep, txt *name, txt *value)
{
	txt work;

	AN(where.b);
	AN(name);
	AN(value);

	work.b = where.b;
	work.e = where.e;

	if (http_split(&work, sep, name) == 0) {
		/*
		 * only whitespace or seperator found: if the where arguemnt
		 * came from http_split, it implies we only have a seperator
		 * with no name or value
		 */
		value->b = NULL;
		value->e = NULL;
		return (1);
	}

	if (work.b >= where.e)
		/* no seperator found */
		return (0);

	(void)http_split(&work, sep, value);

	return (1);
}
// XXX turn off logging?
static inline void
vesico_warn(struct sess *sp, struct vesico_req *m,
	    unsigned action, const char *warn,
	    const txt hdr, const txt where) {
	txt		phdr;
	unsigned	off;

	assert(action > VESICO_OK);
	assert(action < VESICO_WARN_LIM);

	vesico_add_warn(m, action);

	assert(where.b >= hdr.b);
	assert(where.b < hdr.e);
	off = where.b - hdr.b;

	phdr.b = NULL;
	if (off > 20) {
		phdr.b = hdr.b + (off - 20);
		off = 20;
		phdr.e = hdr.e;
	}

	WSP(sp, SLT_VCL_error,
	    "vmod esicookies http0 %s in hdr:", vesico_warn_str[action]);
	if (phdr.b) {
		WSP(sp, SLT_VCL_error,
		    "...%.40s%s", phdr.b,
		    ((phdr.e - phdr.b) > 40) ? "..." : "");
		off += 3;
	} else {
		WSP(sp, SLT_VCL_error,
		    "%.40s%s", hdr.b, ((hdr.e - hdr.b) > 40) ? "..." : "");
	}
	WSP(sp, SLT_VCL_error,
	    "%*s^- %s", off, "", warn);
}

static int
vesico_analyze_cookie_header(struct sess *sp, struct vesico_req *m,
			     const txt hdr, struct cookiehead *cookies,
			     struct cookies *cs) {
	txt		work1, elem, name, value;
	struct cookie	*c, *c2;
	unsigned	ret = VESICO_OK;

	work1.b = hdr.b;
	work1.e = hdr.e;

	while (http_split(&work1, ";", &elem)) {
		if (elem.b == NULL) {
			assert(elem.e == NULL);
			continue;
		}

		assert(Tlen(elem) > 0);

		if (http_nv(elem, "=", &name, &value) == 0) {
			vesico_warn(sp, m, VESICO_WARN_SKIPPED,
			    "no equals", hdr, elem);
			continue;
		}

		if (name.b == NULL) {
			vesico_warn(sp, m, VESICO_WARN_SKIPPED,
			    "no name", hdr, elem);
			continue;
		}

		assert(Tlen(name) > 0);

		if (value.b == NULL)
			vesico_warn(sp, m, VESICO_WARN_TOLERATED,
			    "empty cookie value", hdr, name);
		else
			assert(Tlen(value) > 0);

		if (cs->used >= max_cookies) {
			ret |= VESICO_ERR_OVERFLOW;
			return (ret);
		}

		c = &cs->space[cs->used++];

		c->valid = 0;

		assert(name.b);
		assert(name.e);
		// cant be empty
		assert(name.e > name.b);
		assert(name.b >= hdr.b);
		// and not last in hdr
		assert(name.e < hdr.e);

		if (value.b != NULL) {
			assert(value.b);
			assert(value.e);
			// can be length 0
			assert(value.e >= value.b);
			// but not first in hdr
			assert(value.b > hdr.b);

			assert(value.e <= hdr.e);
		}


		c->name.b = name.b;
		c->name.e = name.e;
		c->value.b = value.b;
		c->value.e = value.e;

		/* check if seen before and, if yes, make that one invalid */
		c2 = vesico_cookie_lookup(cookies, c->name);
		if (c2)
			c2->valid = 0;

		c->valid = 1;
		VSTAILQ_INSERT_TAIL(cookies, c, list);
	}
	return (ret);
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
 * returns error or NULL
 */

const char *H_COOKIE = "\007Cookie:";

static const char *
vesico_write_cookie_hdr(struct sess *sp, struct vesico_req *m,
			struct http *h0, struct cookiehead *cookies)
{
	unsigned	u;
	char *b = NULL, *e = NULL;
	struct cookie	*c;
	struct ws	*ws = vesico_get_mem(sp, m);

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

		unsigned l = Tlen(c->name) +
		    (c->value.b ? Tlen(c->value) : 0);

		if (! (b + l + 2 < e)) {
			WS_Release(ws, 0);
			return "new cookies dont fit";
		};

		l = Tlen(c->name);
		memcpy(b, c->name.b, l);
		b += l;

		*b++ = '=';

		if (c->value.b) {
			l = Tlen(c->value);
			memcpy(b, c->value.b, l);
			b += l;
		}

		b[0] = ';'; b[1] = ' ';
		b += 2;
	}
	b -= 2;
	*b++ = '\0';

	http_Unset(h0, H_COOKIE);
	http_SetHeader(sp->wrk, sp->fd, h0, ws->f);

	WS_ReleaseP(ws, b);

	return (NULL);
}

/*
 * XXX TODO optimize for the case where existing cookie is tail of CS and we
 * don't change existing cookies ?
 */

static const char *
vesico_to_http0(struct sess *sp, struct vmod_priv *priv, enum gethdr_e where,
		const char *hdr)
{
	struct vesico_req	*m;
	struct cookiehead	cookies =
	    VSTAILQ_HEAD_INITIALIZER(cookies);
	struct cookies	cs;

	struct http		*hp, *h0;
	unsigned		n;
	int			used;

	unsigned		ret;

	cs.used = 0;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);

	m = vesico_req(sp, priv->priv);

	vesico_clear_warn(m);
	ret = VESICO_OK;

	h0 = sp->http0;
	CHECK_OBJ_NOTNULL(h0, HTTP_MAGIC);

	/* collect existing cookies */
	for (n = HTTP_HDR_FIRST; n < h0->nhd; n++) {
		if (http_IsHdr(&h0->hd[n], H_COOKIE)) {
			txt			h;

			Tcheck(h0->hd[n]);
			h.b = h0->hd[n].b + *H_COOKIE;
			while (isspace(*(h.b)))
				h.b++;
			h.e = h0->hd[n].e;

			ret = vesico_analyze_cookie_header(sp, m, h, &cookies,
			    &cs);
			if (ret != VESICO_OK)
				return (vesico_err_str[ret]);
		}
	}

	used = cs.used;

	/* collect cookies from the set-cookie hdr given */
	hp = vrt_selecthttp(sp, where);
	for (n = HTTP_HDR_FIRST; n < hp->nhd; n++) {
		if (http_IsHdr(&hp->hd[n], hdr)) {
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

			ret = vesico_analyze_cookie_header(sp, m, h, &cookies,
			    &cs);
			if (ret != VESICO_OK)
				return (vesico_err_str[ret]);
		}
	}

	/* if we haven't used any more cookies than we already had we're done */
	if (used == cs.used)
		return NULL;

	return (vesico_write_cookie_hdr(sp, m, h0, &cookies));
}

const char * __match_proto__()
vmod_to_http0_e(struct sess *sp, struct vmod_priv *priv, enum gethdr_e where,
		const char *hdr) {
	return (vesico_to_http0(sp, priv, where, hdr));
}

const char * __match_proto__()
vmod_warnings(struct sess *sp, struct vmod_priv *priv) {
	struct vesico_req	*m;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	m = vesico_req(sp, priv->priv);

	assert(m->warn < VESICO_WARN_LIM);

	if (m->warn == VESICO_OK)
		return NULL;

	return (vesico_warn_str[m->warn]);
}

void __match_proto__()
vmod_to_http0(struct sess *sp, struct vmod_priv *priv, enum gethdr_e where,
	      const char *hdr) {
	return (void)(vesico_to_http0(sp, priv, where, hdr));
}
