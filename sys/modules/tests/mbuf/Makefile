# $FreeBSD$

SYSDIR?=${SRCTOP}/sys
.include "${SYSDIR}/conf/kern.opts.mk"

.PATH: ${SYSDIR}/tests/mbuf_test

KMOD=	mbuf_test
SRCS=	mbuf_test.c

.include <bsd.kmod.mk>
