/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 Zhenlei Huang
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NET_ROUTE_ROUTE_CACHE_H_
#define _NET_ROUTE_ROUTE_CACHE_H_

struct route_cache_entry;

struct route_cache {
	struct route_cache_entry	*rce; /* pcpu route cache */
	struct rib_subscription	*rs;
}

struct route_cache_entry {
	struct mtx rt_mtx;
	union {
#ifdef INET
		struct route     ro;
#endif
#ifdef INET6
		struct route_in6 ro6;
#endif
	};
} __aligned(CACHE_LINE_SIZE);

#define route2cache_entry(ro)		__containerof((ro), struct route_cache_entry, ro)

#define ROUTE_CACHE_LOCK(p)	mtx_lock(&(p)->rt_mtx)
#define ROUTE_CACHE_TRYLOCK(p)	mtx_trylock(&(p)->rt_mtx)
#define ROUTE_CACHE_UNLOCK(p)	mtx_unlock(&(p)->rt_mtx)
#define ROUTE_CACHE_GET(p)	zpcpu_get((p))

void route_cache_init(struct route_cache *);
void route_cache_uninit(struct route_cache *);
void route_cache_invalidate(struct route_cache *);

/*
struct rib_subscription * route_cache_subscribe_rib_event(uint32_t, int, struct route_cache *);
void route_cache_unsubscribe_rib_event(struct rib_subscription *);
*/
void route_cache_subscribe_rib_event(uint32_t, int, struct route_cache *);
void route_cache_unsubscribe_rib_event(struct route_cache *);

static inline struct route *
route_cache_acquire(struct route_cache *rc)
{
	struct route_cache_entry *rce;
	struct route *ro = NULL;

	rce = ROUTE_CACHE_GET(rc->rce);
	if (ROUTE_CACHE_TRYLOCK(rce))
		ro = &rce->ro;

	return ro;
}

static inline void
route_cache_release(struct route *ro)
{
	struct route_cache_entry *rce;

	if (ro != NULL) {
		rce = route2cache_entry(ro);
		ROUTE_CACHE_UNLOCK(rce);
	}
}

#endif
