/*-
 * Copyright (c) 2022, Zhenlei Huang <zlei.huang@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Neither the name of Matthew Macy nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/counter.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#define	CHUNK_SIZE	8192



static void
mbuf_test(void)
{
	uint64_t count;
	struct timespec ts_pre, ts_post;
	uint64_t cycles_pre, cycles_post;
	int64_t pass_diff = 0;
	uint64_t totol_cycles = 0;
	struct mbuf **mbs;

	mbs = malloc(sizeof(struct mbuf *) * CHUNK_SIZE, M_TEMP, M_ZERO | M_WAITOK);

	count = 0;
	nanouptime(&ts_pre);
	cycles_pre = rdtscp();
	for (int i = 0; i < CHUNK_SIZE; i++) {
		struct mbuf *m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL)
			continue;

		mbs[i] = m;
		count++;
	}
	cycles_post = rdtscp();
	nanouptime(&ts_post);

	totol_cycles = cycles_post - cycles_pre;
	pass_diff = (ts_post.tv_sec - ts_pre.tv_sec) * 1000000000 +
	            (ts_post.tv_nsec - ts_pre.tv_nsec);

	printf("%zu m_gethdr() in %zu nanoseconds, %zu cps, %zu cpc\n",
	       count, pass_diff, count * 1000000000 / pass_diff, totol_cycles / count);



	count = 0;
	nanouptime(&ts_pre);
	cycles_pre = rdtscp();
	for (int i = 0; i < CHUNK_SIZE; i++) {
		if (mbs[i] != NULL) {
			m_freem(mbs[i]);
			count++;
		}
	}
	cycles_post = rdtscp();
	nanouptime(&ts_post);

	totol_cycles = cycles_post - cycles_pre;
	pass_diff = (ts_post.tv_sec - ts_pre.tv_sec) * 1000000000 +
	            (ts_post.tv_nsec - ts_pre.tv_nsec);

	printf("%zu m_freem() in %zu nanoseconds, %zu cps, %zu cpc\n",
	       count, pass_diff, count * 1000000000 / pass_diff, totol_cycles / count);

	free(mbs, M_TEMP);
	return;
}

static int
mbuf_test_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error, v;

	v = 0;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error)
		return (error);
	if (req->newptr == NULL)
		return (error);
	if (v == 0)
		return (0);

	printf("%s\n", "run mbuf test ...");
	mbuf_test();
	printf("%s\n", "run mbuf test done!");

	return (0);
}



SYSCTL_NODE(_kern, OID_AUTO, mbuftest, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "Mbuf Test Framework");
SYSCTL_PROC(_kern_mbuftest, OID_AUTO, runtest,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE,
    0, 0, mbuf_test_sysctl, "I",
    "Execute an mbuf test");

static int
mbuf_test_module_event_handler(module_t mod, int what, void *arg __unused)
{
	switch (what) {
	case MOD_LOAD:
		break;
	case MOD_UNLOAD:
		break;
	default:
		return (EOPNOTSUPP);
	}

	return (0);
}

static moduledata_t mbuf_test_moduledata = {
	"mbuf_test",
	mbuf_test_module_event_handler,
	NULL
};

MODULE_VERSION(mbuf_test, 1);
DECLARE_MODULE(mbuf_test, mbuf_test_moduledata, SI_SUB_PSEUDO, SI_ORDER_ANY);
