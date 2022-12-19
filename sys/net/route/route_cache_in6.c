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
#include <net/route/route_ctl.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>

#include <net/route/route_cache.h>

#include <vm/uma.h>


static uma_zone_t route_cache_pcpu6_zone;

static void
route_cache_pcpu6_zone_init(void)
{
	route_cache_pcpu6_zone = uma_zcreate("route-cache-pcpu6",
            sizeof(struct route_cache_pcpu6), NULL, NULL, NULL, NULL,
            UMA_ALIGN_PTR, UMA_ZONE_PCPU);
}

SYSINIT(route_cache_pcpu6_zone_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_FIRST,
    route_cache_pcpu6_zone_init, NULL);

static void
route_cache_pcpu6_zone_uninit(void)
{
	uma_zdestroy(route_cache_pcpu6_zone);
}

SYSUNINIT(route_cache_pcpu6_zone_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY,
    route_cache_pcpu6_zone_uninit, NULL);


void
route_cache_init_in6(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu6 *pcpu, *rc_pcpu;

	KASSERT((rc->pcpu6 == NULL), ("route cache has been inited"));
	KASSERT((rc->rs == NULL), ("route cache has subscribed rib event"));
	rc_pcpu = uma_zalloc_pcpu(route_cache_pcpu6_zone, M_WAITOK | M_ZERO);
	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc_pcpu, cpu);
		mtx_init(&pcpu->mtx, "route_cache_pcpu6_mtx", NULL, MTX_DEF);
		pcpu->ro6.ro_flags = RT_LLE_CACHE; /* Cache L2 as well */
	}
	rc->pcpu6 = rc_pcpu;
}

void
route_cache_uninit_in6(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu6 *pcpu;

	KASSERT((rc->rs == NULL), ("should unsubscribe rib event before uninit"));
	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc->pcpu6, cpu);
		mtx_assert(&pcpu->mtx, MA_NOTOWNED);
		// XXX use atomic_thread_fence_acq ?
		mtx_lock(&pcpu->mtx);
		RO_INVALIDATE_CACHE(&pcpu->ro6);
		mtx_unlock(&pcpu->mtx);
		mtx_destroy(&pcpu->mtx);
	}
	uma_zfree_pcpu(route_cache_pcpu6_zone, rc->pcpu6);
	rc->pcpu6 = NULL;
}

void
route_cache_invalidate_in6(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu6 *pcpu;

	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc->pcpu6, cpu);
		mtx_lock(&pcpu->mtx);
		RO_INVALIDATE_CACHE(&pcpu->ro6);
		mtx_unlock(&pcpu->mtx);
	}
}

static void
route_cache_subscription_cb_in6(struct rib_head *rnh __unused,
    struct rib_cmd_info *rci __unused, void *arg)
{
	struct route_cache *rc = arg;
	// XXX revalidate should be enough, NH_VALIDATE
	route_cache_invalidate_in6(rc);
}

void
route_cache_subscribe_rib_event_in6(struct route_cache *rc, uint32_t fibnum)
{
	KASSERT((rc->rs == NULL), ("already subscribed rib event"));
	rc->rs = rib_subscribe(fibnum, AF_INET6, route_cache_subscription_cb_in6,
	    rc, RIB_NOTIFY_IMMEDIATE, true);
}
