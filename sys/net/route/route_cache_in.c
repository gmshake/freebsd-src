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

#define RC_INTERNAL 1
#include <net/route/route_cache.h>

#include <vm/uma.h>


static uma_zone_t route_cache_pcpu_zone;

static void
route_cache_pcpu_zone_init(void)
{
	route_cache_pcpu_zone = uma_zcreate("route-cache-pcpu",
	    sizeof(struct route_cache_pcpu), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, UMA_ZONE_PCPU);
}

SYSINIT(route_cache_pcpu_zone_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_FIRST,
    route_cache_pcpu_zone_init, NULL);

static void
route_cache_pcpu_zone_uninit(void)
{
	uma_zdestroy(route_cache_pcpu_zone);
}

SYSUNINIT(route_cache_pcpu_zone_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY,
    route_cache_pcpu_zone_uninit, NULL);


void
route_cache_init_in(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu *pcpu, *rc_pcpu;

	KASSERT((rc->pcpu == NULL), ("route cache has been inited"));
	KASSERT((rc->rs == NULL), ("route cache has subscribed rib event"));
	rc_pcpu = uma_zalloc_pcpu(route_cache_pcpu_zone, M_WAITOK | M_ZERO);
	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc_pcpu, cpu);
		mtx_init(&pcpu->mtx, "route_cache_pcpu_mtx", NULL, MTX_DEF);
		pcpu->ro.ro_flags = RT_LLE_CACHE; /* Cache L2 as well */
	}
	rc->pcpu = rc_pcpu;
}

void
route_cache_uninit_in(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu *pcpu;

	KASSERT((rc->rs == NULL), ("should unsubscribe rib event before uninit"));
	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc->pcpu, cpu);
		mtx_assert(&pcpu->mtx, MA_NOTOWNED);
		// XXX use atomic_thread_fence_acq ?
		mtx_lock(&pcpu->mtx);
		RO_INVALIDATE_CACHE(&pcpu->ro);
		mtx_unlock(&pcpu->mtx);
		mtx_destroy(&pcpu->mtx);
	}
	uma_zfree_pcpu(route_cache_pcpu_zone, rc->pcpu);
	rc->pcpu = NULL;
}

void
route_cache_invalidate_in(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu *pcpu;

	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc->pcpu, cpu);
		mtx_lock(&pcpu->mtx);
		RO_INVALIDATE_CACHE(&pcpu->ro);
		mtx_unlock(&pcpu->mtx);
	}
}

void
route_cache_revalidate_in(struct route_cache *rc)
{
	int cpu;
	struct route_cache_pcpu *pcpu;

	CPU_FOREACH(cpu) {
		pcpu = zpcpu_get_cpu(rc->pcpu, cpu);
		if (mtx_trylock(&pcpu->mtx) && pcpu->ro.ro_nh != NULL) {
			NH_VALIDATE(&pcpu->ro, &pcpu->ro.ro_cookie, rc->fibnum);
			mtx_unlock(&pcpu->mtx);
		}
	}
}
