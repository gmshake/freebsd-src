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
#include <net/route.h>
#include <net/route/nhop.h>
#include <net/route/route_cache.h>
#include <net/route/route_ctl.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>

#include <vm/uma.h>

static uma_zone_t pcpu_route_cache_zone;

static void
route_cache_zone_init(void)
{
	pcpu_route_cache_zone = uma_zcreate("pcpu-route-cache",
	    sizeof(struct route_cache), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, UMA_ZONE_PCPU);
}

SYSINIT(route_cache_zone_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_FIRST,
    route_cache_zone_init, NULL);

static void
route_cache_zone_uninit(void)
{
	uma_zdestroy(pcpu_route_cache_zone);
}

SYSUNINIT(route_cache_zone_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY,
    route_cache_zone_uninit, NULL);

struct route_cache *
route_cache_alloc(int flags)
{
	int i;
	struct route_cache *c;
	struct route_cache *pcpu_rc = uma_zalloc_pcpu(pcpu_route_cache_zone, flags | M_ZERO);
	critical_enter();
	CPU_FOREACH(i) {
		c = zpcpu_get_cpu(pcpu_rc, i);
		c->ro.ro_flags = RT_LLE_CACHE; /* Cache L2 as well */
		mtx_init(&c->rt_mtx, "cache_route_mtx", NULL, MTX_DEF);
	}
	critical_exit();
	return (pcpu_rc);
}

void
route_cache_free(struct route_cache *pcpu_rc)
{
	int i;
	struct route_cache *c;
	CPU_FOREACH(i) {
		c = zpcpu_get_cpu(pcpu_rc, i);
		mtx_lock(&c->rt_mtx);
		RO_INVALIDATE_CACHE(&c->ro);
		mtx_unlock(&c->rt_mtx);
		mtx_destroy(&c->rt_mtx);
	}
	uma_zfree_pcpu(pcpu_route_cache_zone, pcpu_rc);
}

void
route_cache_invalidate(struct route_cache *pcpu_rc)
{
	int i;
	struct route_cache *c;
	CPU_FOREACH(i) {
		c = zpcpu_get_cpu(pcpu_rc, i);
		mtx_lock(&c->rt_mtx);
		RO_INVALIDATE_CACHE(&c->ro);
		mtx_unlock(&c->rt_mtx);
	}
}

static void
route_cache_subscription_cb(struct rib_head *rnh, struct rib_cmd_info *rc,
    void *arg)
{
	struct route_cache *pcpu_rc = arg;
	route_cache_invalidate(pcpu_rc);
}

struct rib_subscription *
route_cache_subscribe_rib_event(uint32_t fibnum, int family,
    struct route_cache *pcpu_rc)
{
	return (rib_subscribe(fibnum, family, route_cache_subscription_cb,
	    pcpu_rc, RIB_NOTIFY_IMMEDIATE, true));
}

void
route_cache_unsubscribe_rib_event(struct rib_subscription *rs)
{
	rib_unsibscribe(rs);
}
