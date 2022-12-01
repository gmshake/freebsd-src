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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/rmlock.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_llatbl.h>
#include <net/route.h>
#include <net/route/nhop.h>
#include <net/route/route_ctl.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet6/in6_var.h>

#include <net/route/route_cache.h>

#include <vm/uma.h>


static MALLOC_DEFINE(M_ROUTECACHE, "route-cache", "Route Cache");

struct route_cache_entry {
	rt_gen_t cookie;
	uint32_t fibnum;
	union {
		struct route ro;
#ifdef INET
		struct route_in ro4;
#endif
#ifdef INET6
		struct route_in6 ro6;
#endif
	};
	struct epoch_context epoch_ctx;
};

struct route_cache {
	struct rmlock lock;
	struct route_cache_entry *entry; // XXX volitile ???
	struct rib_subscription *rs;
};

static void route_cache_entry_free(struct route_cache_entry *);
static void route_cache_entry_free_epoch(epoch_context_t);

struct route_cache *
route_cache_alloc(void)
{
	struct route_cache *rc;

	rc = malloc(sizeof(struct route_cache), M_ROUTECACHE, M_WAITOK | M_ZERO);
	rm_init(&rc->lock, "route-cache-lock");
	return rc;
}

void
route_cache_free(struct route_cache *rc)
{
	KASSERT((rc->rs == NULL), ("should unsubscribe rib event before free"));

	NET_EPOCH_WAIT();
	if (rc->entry != NULL)
		route_cache_entry_free(rc->entry);

	rm_destroy(&rc->lock);
	free(rc, M_ROUTECACHE);
}

/* rte<>ro_flags translation */
static inline void
rt_update_ro_flags(struct route *ro, const struct nhop_object *nh)
{
	int nh_flags = nh->nh_flags;

	ro->ro_flags &= ~ (RT_REJECT|RT_BLACKHOLE|RT_HAS_GW);

	ro->ro_flags |= (nh_flags & NHF_REJECT) ? RT_REJECT : 0;
	ro->ro_flags |= (nh_flags & NHF_BLACKHOLE) ? RT_BLACKHOLE : 0;
	ro->ro_flags |= (nh_flags & NHF_GATEWAY) ? RT_HAS_GW : 0;
}

static struct route_cache_entry *
route_cache_entry_alloc(uint32_t fibnum, struct in_addr addr, uint32_t flowid)
{
	struct route_cache_entry *entry;
	struct route *ro;
	struct nhop_object *nh;

	// XXX M_WAITOK or M_NOWAIT
	entry = malloc(sizeof(struct route_cache_entry), M_ROUTECACHE, M_WAITOK | M_ZERO);
	entry->fibnum = fibnum;
	// lock route table
	entry->cookie = rt_tables_get_gen(fibnum, AF_INET);
	// unlock route table

	nh = fib4_lookup(fibnum, addr, 0, NHR_REF, flowid);

	ro = &entry->ro;
	ro->ro_nh = nh;

	struct sockaddr_in *dst = (struct sockaddr_in *)&ro->ro_dst;
	dst->sin_family = AF_INET;
	dst->sin_addr = addr;

	if (nh != NULL)
		rt_update_ro_flags(ro, nh);
	return entry;
}

static void
route_cache_entry_free(struct route_cache_entry *entry)
{
	struct route *ro = &entry->ro;

	if (ro->ro_lle != NULL)
		LLE_FREE(ro->ro_lle);
	if (ro->ro_nh != NULL)
		nhop_free(ro->ro_nh);
	free(entry, M_ROUTECACHE);
}

static void
route_cache_entry_free_epoch(epoch_context_t ctx)
{
	struct route_cache_entry *entry;

	entry = __containerof(ctx, struct route_cache_entry, epoch_ctx);
	route_cache_entry_free(entry);
}

struct route *
route_cache_validate_in(struct route_cache *rc, uint32_t fibnum, uint32_t s_addr)
{
	struct rm_priotracker tracker;
	struct route_cache_entry *entry;
	struct route *ro = NULL;

	if (!rm_try_rlock(&rc->lock, &tracker))
		return (NULL);

	entry = rc->entry;
	/*
	 * FIXME route_cache_entry is protected under network epoch,
	 * still need read lock here ?
	 */
	if (entry == NULL)
		goto out;

	if (entry->fibnum != fibnum)
		goto out;

	/* XXX short cut, check dst->sin_family and dst->sin_addr */
	struct sockaddr_in *dst = (struct sockaddr_in *)&entry->ro.ro_dst;
	if (dst->sin_family != AF_INET || dst->sin_addr.s_addr != s_addr)
		goto out;

	rt_gen_t cookie = rt_tables_get_gen(fibnum, AF_INET);
	if (entry->cookie != cookie)
		goto out;

	struct nhop_object *nh = entry->ro.ro_nh;
	if (nh == NULL || (NH_IS_VALID(nh)))
		ro = &entry->ro; /* Now we are confidental that the route is valid */

out:
	rm_runlock(&rc->lock, &tracker);
	return (ro);
}

void
route_cache_update_in(struct route_cache *rc, uint32_t fibnum, struct in_addr addr, uint32_t flowid)
{
	struct route_cache_entry *old, *new;

	new = route_cache_entry_alloc(fibnum, addr, flowid);
	// XXX atomic exchange rc->entry ???
	rm_wlock(&rc->lock);
	old = rc->entry;
	rc->entry = new; /* FIXME check old before replace new entry ??? */
	rm_wunlock(&rc->lock);

	if (old != NULL)
		NET_EPOCH_CALL(route_cache_entry_free_epoch, &old->epoch_ctx);
}

void
route_cache_invalidate(struct route_cache *rc)
{
	struct route_cache_entry *entry;

	// XXX atomic exchange rc->entry ???
	rm_wlock(&rc->lock);
	entry = rc->entry;
	rc->entry = NULL;
	rm_wunlock(&rc->lock);

	if (entry != NULL)
		NET_EPOCH_CALL(route_cache_entry_free_epoch, &entry->epoch_ctx);
}

static void
route_cache_subscription_cb(struct rib_head *rnh __unused,
    struct rib_cmd_info *rci __unused, void *arg)
{
	struct route_cache *rc = arg;

	// invalidate or test and update ???
	route_cache_invalidate(rc);
}

void
route_cache_subscribe_rib_event(struct route_cache *rc, int family,
    uint32_t fibnum)
{
	KASSERT((rc->rs == NULL), ("already subscribed rib event"));
	rc->rs = rib_subscribe(fibnum, family, route_cache_subscription_cb,
	    rc, RIB_NOTIFY_IMMEDIATE, true);
}

void
route_cache_unsubscribe_rib_event(struct route_cache *rc)
{
	struct epoch_tracker et;

	KASSERT((rc->rs != NULL), ("not subscribed rib event"));
	NET_EPOCH_ENTER(et);
	rib_unsubscribe(rc->rs);
	NET_EPOCH_EXIT(et);
	rc->rs = NULL;
}
