#	$OpenBSD: Makefile,v 1.5 2020/04/06 09:43:55 dtucker Exp $

PROG=test_authopt
SRCS=tests.c

SRCS+=auth-options.c

# From usr.bin/ssh
SRCS+=sshbuf-getput-basic.c sshbuf-getput-crypto.c sshbuf-misc.c sshbuf.c
SRCS+=sshbuf-io.c atomicio.c sshkey.c authfile.c cipher.c log.c ssh-rsa.c
SRCS+=ssh-dss.c ssh-ecdsa.c ssh-ed25519.c mac.c umac.c umac128.c hmac.c misc.c
SRCS+=ssherr.c uidswap.c cleanup.c xmalloc.c match.c krl.c fatal.c
SRCS+=addrmatch.c bitmap.c
SRCS+=ed25519.c hash.c ge25519.c fe25519.c sc25519.c verify.c
SRCS+=cipher-chachapoly.c chacha.c poly1305.c

SRCS+=digest-openssl.c
#SRCS+=digest-libc.c
SRCS+=utf8.c

REGRESS_TARGETS=run-regress-${PROG}

run-regress-${PROG}: ${PROG}
	env ${TEST_ENV} ./${PROG} -d ${.CURDIR}/testdata

.include <bsd.regress.mk>
