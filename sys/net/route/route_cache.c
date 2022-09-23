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
#include <sys/sysctl.h>

#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/queue.h>
#include <sys/smp.h>
#include <sys/socket.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_llatbl.h>
#include <net/vnet.h>
#include <net/route.h>
#include <net/route/nhop.h>
#include <net/route/route_ctl.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>

#include <net/route/route_cache.h>

#include <vm/uma.h>


VNET_DEFINE(u_int, route_cache) = 1;
SYSCTL_DECL(_net_route);
SYSCTL_UINT(_net_route, OID_AUTO, cache, CTLFLAG_RW | CTLFLAG_VNET,
    &VNET_NAME(route_cache), 0, "Enable route cache");

static uma_zone_t pcpu_route_cache_entry_zone;

static void
route_cache_entry_zone_init(void)
{
	pcpu_route_cache_entry_zone = uma_zcreate("pcpu-route-cache-entry",
	    sizeof(struct route_cache_entry), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, UMA_ZONE_PCPU);
}

SYSINIT(route_cache_entry_zone_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_FIRST,
    route_cache_entry_zone_init, NULL);

static void
route_cache_entry_zone_uninit(void)
{
	uma_zdestroy(pcpu_route_cache_entry_zone);
}

SYSUNINIT(route_cache_entry_zone_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY,
    route_cache_entry_zone_uninit, NULL);

void
route_cache_init(struct route_cache *rc)
{
	int cpu;
	struct route_cache_entry *e;
	struct route_cache_entry *pcpu_rce = uma_zalloc_pcpu(pcpu_route_cache_entry_zone, M_WAITOK | M_ZERO);
	critical_enter();
	CPU_FOREACH(cpu) {
		e = zpcpu_get_cpu(pcpu_rce, cpu);
		e->ro.ro_flags = RT_LLE_CACHE; /* Cache L2 as well */
		mtx_init(&e->rt_mtx, "cache_route_mtx", NULL, MTX_DEF);
	}
	critical_exit();
	rc->rce = pcpu_rce;
	rc->rs = NULL;
}

void
route_cache_uninit(struct route_cache *rc)
{
	int cpu;
	struct route_cache_entry *e;
	CPU_FOREACH(cpu) {
		e = zpcpu_get_cpu(rc->rce, cpu);
		mtx_lock(&e->rt_mtx);
		RO_INVALIDATE_CACHE(&e->ro);
		mtx_unlock(&e->rt_mtx);
		mtx_destroy(&e->rt_mtx);
	}
	uma_zfree_pcpu(pcpu_route_cache_entry_zone, rc->rce);
	rc->rce = NULL; /* XXX change to 0xdeadc0de */
}

void
route_cache_invalidate(struct route_cache *rc)
{
	int cpu;
	struct route_cache_entry *e;
	CPU_FOREACH(cpu) {
		e = zpcpu_get_cpu(rc->rce, cpu);
		mtx_lock(&e->rt_mtx);
		RO_INVALIDATE_CACHE(&e->ro);
		mtx_unlock(&e->rt_mtx);
	}
}

static void
route_cache_subscription_cb(struct rib_head *rnh __unused,
    struct rib_cmd_info *rci __unused, void *arg)
{
	struct route_cache *rc = arg;
	route_cache_invalidate(rc);
}

void
route_cache_subscribe_rib_event(uint32_t fibnum, int family,
    struct route_cache *rc)
{
	KASSERT((rc->rs == NULL), ("already subscribed rib event"));
	rc->rs = rib_subscribe(fibnum, family, route_cache_subscription_cb,
	    rc, RIB_NOTIFY_IMMEDIATE, true);
}

void
route_cache_unsubscribe_rib_event(struct route_cache *rc)
{
	struct epoch_tracker et;

	KASSERT((rc->rs != NULL), ("not subscribed"));
	NET_EPOCH_ENTER(et);
	rib_unsubscribe(rc->rs);
	rc->rs = NULL;
	NET_EPOCH_EXIT(et);
}
