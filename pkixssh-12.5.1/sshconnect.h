/* $OpenBSD: sshconnect.h,v 1.40 2020/01/25 07:17:18 djm Exp $ */

/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
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

typedef struct Sensitive Sensitive;
struct Sensitive {
	struct sshkey	**keys;
	int		nkeys;
};

struct addrinfo;
struct ssh;

int	 ssh_connect(struct ssh *, const char *, const char *,
	    struct addrinfo *, struct sockaddr_storage *, u_short,
	    int, int, int *, int);
void	 ssh_kill_proxy_command(void);

void	 ssh_login(struct ssh *, Sensitive *, const char *,
    struct sockaddr *, u_short, struct passwd *, int);

int	 verify_host_key(char *host, struct sockaddr *hostaddr,
    char *hostkey_alg, struct sshkey *host_key);

void	 get_hostfile_hostname_ipaddr(char *, struct sockaddr *, u_short,
    char **, char **);

void	 ssh_kex2(struct ssh *ssh, char *, struct sockaddr *, u_short);

void	 ssh_userauth2(struct ssh *ssh, const char *, const char *,
    char *, Sensitive *);

int	 ssh_local_cmd(const char *);

void	 maybe_add_key_to_agent(const char *, struct sshkey *,
    const char *, const char *);
