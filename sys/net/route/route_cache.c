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


void
route_cache_init(struct route_cache *rc, int family)
{
	switch (family) {
#ifdef INET
	case AF_INET:
		route_cache_init_in(rc);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		route_cache_init_in6(rc);
		break;
#endif
	default:
		// Unreachable
		panic("Unsupported af: %d", family);
	}
}

void
route_cache_uninit(struct route_cache *rc, int family)
{
	switch (family) {
#ifdef INET
	case AF_INET:
		route_cache_uninit_in(rc);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		route_cache_uninit_in6(rc);
		break;
#endif
	default:
		// Unreachable
		panic("Unsupported af: %d", family);
	}
}

void
route_cache_invalidate(struct route_cache *rc, int family)
{
	switch (family) {
#ifdef INET
	case AF_INET:
		route_cache_invalidate_in(rc);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		route_cache_invalidate_in6(rc);
		break;
#endif
	default:
		// Unreachable
		panic("Unsupported af: %d", family);
	}
}

void
route_cache_subscribe_rib_event(struct route_cache *rc, int family,
    uint32_t fibnum)
{
	switch (family) {
#ifdef INET
	case AF_INET:
		route_cache_subscribe_rib_event_in(rc, fibnum);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		route_cache_subscribe_rib_event_in6(rc, fibnum);
		break;
#endif
	default:
		// Unreachable
		panic("Unsupported af: %d", family);
	}
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
