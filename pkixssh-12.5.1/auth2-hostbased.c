/* $OpenBSD: auth2-hostbased.c,v 1.42 2019/11/25 00:51:37 djm Exp $ */
/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * X.509 certificates support,
 * Copyright (c) 2004-2019 Roumen Petrov.  All rights reserved.
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

#include <sys/types.h>

#include <stdlib.h>
#include <pwd.h>
#include <string.h>
#include <stdarg.h>

#include "xmalloc.h"
#include "ssh2.h"
#include "packet.h"
#include "sshbuf.h"
#include "log.h"
#include "misc.h"
#include "servconf.h"
#include "compat.h"
#include "sshxkey.h"
#include "hostfile.h"
#include "auth.h"
#include "canohost.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "pathnames.h"
#include "ssherr.h"
#include "match.h"

/* import */
extern ServerOptions options;
extern u_char *session_id2;
extern u_int session_id2_len;

/* return 1 if given hostbased algorithm is allowed */
static int
hostbased_algorithm_allowed(const char* pkalg)
{
	if (
	    (options.hostbased_algorithms != NULL) &&
	    (match_pattern_list(pkalg, options.hostbased_algorithms, 0) != 1)
	) {
		debug("disallowed hostbased algorithm: %s", pkalg);
		return 0;
	}

	if (
	    (options.accepted_algorithms != NULL) &&
	    (match_pattern_list(pkalg, options.accepted_algorithms, 0) != 1)
	) {
		debug("non-accepted hostbased algorithm: %s", pkalg);
		return 0;
	}

	return 1;
}

static int
userauth_hostbased(struct ssh *ssh)
{
	Authctxt *authctxt = ssh->authctxt;
	struct sshbuf *b;
	struct sshkey *key = NULL;
	char *pkalg = NULL, *cuser = NULL, *chost = NULL;
	u_char *pkblob = NULL, *sig = NULL;
	size_t alen, blen, slen;
	int r, pktype, authenticated = 0;

	/* XXX use sshkey_froms() */
	if ((r = sshpkt_get_cstring(ssh, &pkalg, &alen)) != 0 ||
	    (r = sshpkt_get_string(ssh, &pkblob, &blen)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &chost, NULL)) != 0 ||
	    (r = sshpkt_get_cstring(ssh, &cuser, NULL)) != 0 ||
	    (r = sshpkt_get_string(ssh, &sig, &slen)) != 0)
		fatal("%s: packet parsing: %s", __func__, ssh_err(r));

	debug("%s: cuser %s chost %s pkalg %s slen %zu", __func__,
	    cuser, chost, pkalg, slen);
#ifdef DEBUG_PK
	debug("signature:");
	sshbuf_dump_data(sig, slen, stderr);
#endif
	pktype = sshkey_type_from_name(pkalg);
	if (pktype == KEY_UNSPEC) {
		/* this is perfectly legal */
		logit("%s: unsupported public key algorithm: %s",
		    __func__, pkalg);
		goto done;
	}
	if (!hostbased_algorithm_allowed(pkalg)) {
		goto done;
	}
	if ((r = Xkey_from_blob(pkalg, pkblob, blen, &key)) != 0) {
		error("%s: could not parse key: %s", __func__, ssh_err(r));
		goto done;
	}
	if (key == NULL) {
		error("%s: cannot decode key: %s", __func__, pkalg);
		goto done;
	}
	if (key->type != pktype) {
		error("%s: type mismatch for decoded key "
		    "(received %d, expected %d)", __func__, key->type, pktype);
		goto done;
	}
	if (sshkey_type_plain(key->type) == KEY_RSA &&
	    ssh_compat_fellows(ssh, SSH_BUG_RSASIGMD5)) {
		error("Refusing RSA key because peer uses unsafe "
		    "signature format");
		goto done;
	}
	if ((r = sshkey_check_cert_sigtype(key,
	    options.ca_sign_algorithms)) != 0) {
		logit("%s: certificate signature algorithm %s: %s", __func__,
		    (key->cert == NULL || key->cert->signature_type == NULL) ?
		    "(null)" : key->cert->signature_type, ssh_err(r));
		goto done;
	}

	if (!authctxt->valid || authctxt->user == NULL) {
		debug2("%s: disabled because of invalid user", __func__);
		goto done;
	}

	if ((b = sshbuf_new()) == NULL)
		fatal("%s: sshbuf_new failed", __func__);
	/* reconstruct packet */
	if ((r = sshbuf_put_string(b, session_id2, session_id2_len)) != 0 ||
	    (r = sshbuf_put_u8(b, SSH2_MSG_USERAUTH_REQUEST)) != 0 ||
	    (r = sshbuf_put_cstring(b, authctxt->user)) != 0 ||
	    (r = sshbuf_put_cstring(b, authctxt->service)) != 0 ||
	    (r = sshbuf_put_cstring(b, "hostbased")) != 0 ||
	    (r = sshbuf_put_string(b, pkalg, alen)) != 0 ||
	    (r = sshbuf_put_string(b, pkblob, blen)) != 0 ||
	    (r = sshbuf_put_cstring(b, chost)) != 0 ||
	    (r = sshbuf_put_cstring(b, cuser)) != 0)
		fatal("%s: buffer error: %s", __func__, ssh_err(r));
#ifdef DEBUG_PK
	sshbuf_dump(b, stderr);
#endif

	auth2_record_info(authctxt,
	    "client user \"%.100s\", client host \"%.100s\"", cuser, chost);

	/* test for allowed key and correct signature */
	authenticated = 0;
{	ssh_verify_ctx ctx = { pkalg, key, &ssh->compat, NULL };

	if (PRIVSEP(hostbased_xkey_allowed(ssh, authctxt->pw, &ctx,
	    cuser, chost)) &&
	    PRIVSEP(Xkey_verify(&ctx, sig, slen,
		sshbuf_ptr(b), sshbuf_len(b))) == 0)
		authenticated = 1;
}

	auth2_record_key(authctxt, authenticated, key);
	sshbuf_free(b);
done:
	debug2("%s: authenticated %d", __func__, authenticated);
	sshkey_free(key);
	free(pkalg);
	free(pkblob);
	free(cuser);
	free(chost);
	free(sig);
	return authenticated;
}

/* return 1 if given hostkey is allowed */
int
hostbased_xkey_allowed(struct ssh *ssh, struct passwd *pw,
    ssh_verify_ctx *ctx, const char *cuser, char *chost)
{
	struct sshkey *key = ctx->key;
	const char *resolvedname, *ipaddr, *lookup, *reason;
	HostStatus host_status;
	int len;
	char *fp;

	if (auth_key_is_revoked(key))
		return 0;

	resolvedname = auth_get_canonical_hostname(ssh, options.use_dns);
	ipaddr = ssh_remote_ipaddr(ssh);

	debug2("%s: chost %s resolvedname %s ipaddr %s", __func__,
	    chost, resolvedname, ipaddr);

	if (((len = strlen(chost)) > 0) && chost[len - 1] == '.') {
		debug2("stripping trailing dot from chost %s", chost);
		chost[len - 1] = '\0';
	}

	if (options.hostbased_uses_name_from_packet_only) {
		if (auth_rhosts2(pw, cuser, chost, chost) == 0) {
			debug2("%s: auth_rhosts2 refused "
			    "user \"%.100s\" host \"%.100s\" (from packet)",
			    __func__, cuser, chost);
			return 0;
		}
		lookup = chost;
	} else {
		if (strcasecmp(resolvedname, chost) != 0)
			logit("userauth_hostbased mismatch: "
			    "client sends %s, but we resolve %s to %s",
			    chost, ipaddr, resolvedname);
		if (auth_rhosts2(pw, cuser, resolvedname, ipaddr) == 0) {
			debug2("%s: auth_rhosts2 refused "
			    "user \"%.100s\" host \"%.100s\" addr \"%.100s\"",
			    __func__, cuser, resolvedname, ipaddr);
			return 0;
		}
		lookup = resolvedname;
	}
	debug2("%s: access allowed by auth_rhosts2", __func__);

	if (sshkey_is_cert(key) &&
	    sshkey_cert_check_authority(key, 1, 0, lookup, &reason)) {
		error("%s", reason);
		auth_debug_add("%s", reason);
		return 0;
	}

	host_status = check_key_in_hostfiles(pw, key, lookup,
	    _PATH_SSH_SYSTEM_HOSTFILE,
	    options.ignore_user_known_hosts ? NULL : _PATH_SSH_USER_HOSTFILE);

	/* backward compat if no key has been found. */
	if (host_status == HOST_NEW) {
		host_status = check_key_in_hostfiles(pw, key, lookup,
		    _PATH_SSH_SYSTEM_HOSTFILE2,
		    options.ignore_user_known_hosts ? NULL :
		    _PATH_SSH_USER_HOSTFILE2);
	}

	if (host_status == HOST_OK) {
		if (sshkey_is_cert(key)) {
			if ((fp = sshkey_fingerprint(key->cert->signature_key,
			    options.fingerprint_hash, SSH_FP_DEFAULT)) == NULL)
				fatal("%s: sshkey_fingerprint fail", __func__);
			verbose("Accepted certificate ID \"%s\" signed by "
			    "%s CA %s from %s@%s", key->cert->key_id,
			    sshkey_type(key->cert->signature_key), fp,
			    cuser, lookup);
		} else {
			if ((fp = sshkey_fingerprint(key,
			    options.fingerprint_hash, SSH_FP_DEFAULT)) == NULL)
				fatal("%s: sshkey_fingerprint fail", __func__);
			verbose("Accepted %s public key %s from %s@%s",
			    sshkey_type(key), fp, cuser, lookup);
		}
		free(fp);
	}

	return (host_status == HOST_OK);
}

Authmethod method_hostbased = {
	"hostbased",
	userauth_hostbased,
	&options.hostbased_authentication
};
