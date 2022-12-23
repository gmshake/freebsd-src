/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2022 Zhenlei Huang
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

struct route_cache_pcpu {
	struct mtx mtx;
	union {
		struct route ro;
#ifdef INET
		struct route_in ro4;
#endif
#ifdef INET6
		struct route_in6 ro6;
#endif
	};
} __aligned(CACHE_LINE_SIZE);

struct route_cache {
	struct route_cache_pcpu *pcpu;
	struct rib_subscription *rs;
	int family;
	uint32_t fibnum;
};

void route_cache_init(struct route_cache *, int, uint32_t);
void route_cache_uninit(struct route_cache *);
void route_cache_invalidate(struct route_cache *);
void route_cache_subscribe_rib_event(struct route_cache *);
void route_cache_unsubscribe_rib_event(struct route_cache *);

static inline struct route *
route_cache_acquire(struct route_cache *rc)
{
	struct route_cache_pcpu *pcpu;
	struct route *ro = NULL;

	pcpu = zpcpu_get(rc->pcpu);
	if (mtx_trylock(&pcpu->mtx))
		ro = &pcpu->ro;

	return ro;
}

static inline void
route_cache_release(struct route *ro)
{
	struct route_cache_pcpu *pcpu;

	if (ro != NULL) {
		pcpu = __containerof(ro, struct route_cache_pcpu, ro);
		mtx_assert(&pcpu->mtx, MA_OWNED);
		mtx_unlock(&pcpu->mtx);
	}
}

#ifdef INET6
static inline struct route_in6 *
route_cache_acquire6(struct route_cache *rc)
{
	struct route_cache_pcpu *pcpu;
	struct route_in6 *ro = NULL;

	pcpu = zpcpu_get(rc->pcpu);
	if (mtx_trylock(&pcpu->mtx))
		ro = &pcpu->ro6;

	return ro;
}

static inline void
route_cache_release6(struct route_in6 *ro)
{
	struct route_cache_pcpu *pcpu;

	if (ro != NULL) {
		pcpu = __containerof(ro, struct route_cache_pcpu, ro6);
		mtx_assert(&pcpu->mtx, MA_OWNED);
		mtx_unlock(&pcpu->mtx);
	}
}
#endif

#endif
