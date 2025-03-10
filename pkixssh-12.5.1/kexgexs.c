/* $OpenBSD: kexgexs.c,v 1.42 2019/01/23 00:30:41 djm Exp $ */
/*
 * Copyright (c) 2000 Niels Provos.  All rights reserved.
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 *
 * Copyright (c) 2014-2019 Roumen Petrov.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#ifdef WITH_OPENSSL


#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <openssl/dh.h>

#include "evp-compat.h"
#include "sshxkey.h"
#include "cipher.h"
#include "digest.h"
#include "kex.h"
#include "log.h"
#include "packet.h"
#include "dh.h"
#include "ssh2.h"
#include "compat.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "dispatch.h"
#include "ssherr.h"
#include "sshbuf.h"
#include "misc.h"

static int input_kex_dh_gex_request(int, u_int32_t, struct ssh *);
static int input_kex_dh_gex_init(int, u_int32_t, struct ssh *);

int
kexgex_server(struct ssh *ssh)
{
	ssh_dispatch_set(ssh, SSH2_MSG_KEX_DH_GEX_REQUEST,
	    &input_kex_dh_gex_request);
	debug("expecting SSH2_MSG_KEX_DH_GEX_REQUEST");
	return 0;
}

static int
input_kex_dh_gex_request(int type, u_int32_t seq, struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	int r;
	u_int min = 0, max = 0, nbits = 0;
	const BIGNUM *dh_p, *dh_g;

	UNUSED(type);
	UNUSED(seq);
	debug("SSH2_MSG_KEX_DH_GEX_REQUEST received");
	if ((r = sshpkt_get_u32(ssh, &min)) != 0 ||
	    (r = sshpkt_get_u32(ssh, &nbits)) != 0 ||
	    (r = sshpkt_get_u32(ssh, &max)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		goto out;
	kex->nbits = nbits;
	kex->min = min;
	kex->max = max;
	min = MAXIMUM(DH_GRP_MIN, min);
	max = MINIMUM(DH_GRP_MAX, max);
	nbits = MAXIMUM(DH_GRP_MIN, nbits);
	nbits = MINIMUM(DH_GRP_MAX, nbits);

	if (kex->max < kex->min || kex->nbits < kex->min ||
	    kex->max < kex->nbits || kex->max < DH_GRP_MIN) {
		r = SSH_ERR_DH_GEX_OUT_OF_RANGE;
		goto out;
	}

	/* Contact privileged parent */
	kex->dh = PRIVSEP(choose_dh(min, nbits, max));
	if (kex->dh == NULL) {
		sshpkt_disconnect(ssh, "no matching DH grp found");
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	debug("SSH2_MSG_KEX_DH_GEX_GROUP sent");
	DH_get0_pqg(kex->dh, &dh_p, NULL, &dh_g);
	if ((r = sshpkt_start(ssh, SSH2_MSG_KEX_DH_GEX_GROUP)) != 0 ||
	    (r = sshpkt_put_bignum2(ssh, dh_p)) != 0 ||
	    (r = sshpkt_put_bignum2(ssh, dh_g)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		goto out;

	/* Compute our exchange value in parallel with the client */
	if ((r = dh_gen_key(kex->dh, kex->we_need * 8)) != 0)
		goto out;

	debug("expecting SSH2_MSG_KEX_DH_GEX_INIT");
	ssh_dispatch_set(ssh, SSH2_MSG_KEX_DH_GEX_INIT, &input_kex_dh_gex_init);
	r = 0;
 out:
	return r;
}

static int
input_kex_dh_gex_init(int type, u_int32_t seq, struct ssh *ssh)
{
	struct kex *kex = ssh->kex;
	BIGNUM *dh_client_pub = NULL;
	const BIGNUM *pub_key, *dh_p, *dh_g;
	struct sshbuf *shared_secret = NULL;
	struct sshbuf *server_host_key_blob = NULL;
	struct sshkey *server_host_public, *server_host_private;
	u_char *signature = NULL;
	u_char hash[SSH_DIGEST_MAX_LENGTH];
	size_t slen, hashlen;
	int r;

	UNUSED(type);
	UNUSED(seq);
	r = kex_load_host_keys(ssh, &server_host_public, &server_host_private);
	if (r != SSH_ERR_SUCCESS)
		goto out;

	/* key, cert */
	if ((r = sshpkt_get_bignum2(ssh, &dh_client_pub)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		goto out;
	if ((shared_secret = sshbuf_new()) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if ((r = kex_dh_compute_key(kex, dh_client_pub, shared_secret)) != 0)
		goto out;

	if ((server_host_key_blob = sshbuf_new()) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	r = Xkey_putb(kex->hostkey_alg, server_host_public, server_host_key_blob);
	if (r != SSH_ERR_SUCCESS) goto out;
#ifdef DEBUG_KEXDH
	dump_digestb("server public key:", server_host_key_blob);
#endif

	/* calc H */
	DH_get0_key(kex->dh, &pub_key, NULL);
	DH_get0_pqg(kex->dh, &dh_p, NULL, &dh_g);
	hashlen = sizeof(hash);
	if ((r = kexgex_hash(
	    kex->hash_alg,
	    kex->client_version,
	    kex->server_version,
	    kex->peer,
	    kex->my,
	    server_host_key_blob,
	    kex->min, kex->nbits, kex->max,
	    dh_p, dh_g,
	    dh_client_pub,
	    pub_key,
	    sshbuf_ptr(shared_secret), sshbuf_len(shared_secret),
	    hash, &hashlen)) != 0)
		goto out;

	/* sign H */
{	ssh_sign_ctx ctx = { kex->hostkey_alg, server_host_private, &ssh->compat, NULL };

	r = kex->xsign(ssh, &ctx, server_host_public, &signature, &slen, hash, hashlen);
	if (r != 0)
		goto out;
}

	/* send server hostkey, DH pubkey 'f' and signed H */
	if ((r = sshpkt_start(ssh, SSH2_MSG_KEX_DH_GEX_REPLY)) != 0 ||
	    (r = sshpkt_put_stringb(ssh, server_host_key_blob)) != 0 ||
	    (r = sshpkt_put_bignum2(ssh, pub_key)) != 0 ||     /* f */
	    (r = sshpkt_put_string(ssh, signature, slen)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		goto out;

	if ((r = kex_derive_keys(ssh, hash, hashlen, shared_secret)) == 0)
		r = kex_send_newkeys(ssh);
 out:
	explicit_bzero(hash, sizeof(hash));
	DH_free(kex->dh);
	kex->dh = NULL;
	BN_clear_free(dh_client_pub);
	sshbuf_free(shared_secret);
	sshbuf_free(server_host_key_blob);
	free(signature);
	return r;
}
#endif /* WITH_OPENSSL */
