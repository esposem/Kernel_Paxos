/*
 * Copyright (c) 2013-2015, University of Lugano
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the names of it
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "paxos.h"
#include "evpaxos.h"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h> //kmalloc
#include <linux/unistd.h>
#include <linux/ctype.h>
#include <linux/stat.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <net/sock.h>

struct address
{
	char* addr;
	int port;
};

struct evpaxos_config
{
	int proposers_count;
	int acceptors_count;
	// int learners_count;
	struct address proposers[MAX_N_OF_PROPOSERS];
	struct address acceptors[MAX_N_OF_PROPOSERS];
	// struct address learners[MAX_N_OF_LEARNERS];
};

enum option_type
{
	option_boolean,
	option_integer,
	option_string,
	option_verbosity,
	// option_backend,
	// option_bytes
};

struct option
{
	const char* name;
	void* value;
	enum option_type type;
};

struct option options[] =
{
	{ "verbosity", &paxos_config.verbosity, option_verbosity },
	{ "tcp-nodelay", &paxos_config.tcp_nodelay, option_boolean },
	{ "learner-catch-up", &paxos_config.learner_catch_up, option_boolean },
	{ "proposer-timeout", &paxos_config.proposer_timeout, option_integer },
	{ "proposer-preexec-window", &paxos_config.proposer_preexec_window, option_integer },
	// { "storage-backend", &paxos_config.storage_backend, option_backend },
	{ "acceptor-trash-files", &paxos_config.trash_files, option_boolean },
	// { "lmdb-sync", &paxos_config.lmdb_sync, option_boolean },
	// { "lmdb-env-path", &paxos_config.lmdb_env_path, option_string },
	// { "lmdb-mapsize", &paxos_config.lmdb_mapsize, option_bytes },
	{ 0 }
};

static int parse_line(struct evpaxos_config* c, char* line);
static void address_init(struct address* a, char* addr, int port);
static void address_free(struct address* a);
static void address_copy(struct address* src, struct address* dst);
static struct sockaddr_in address_to_sockaddr(struct address* a);

unsigned int inet_addr(char *str)
{
    int a, b, c, d;
    char arr[4];
    sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
    arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;
    return *(unsigned int *)arr;
}

struct evpaxos_config*
evpaxos_config_read(const char* path)
{
	char * line;
	struct evpaxos_config* c = NULL;

	c = kmalloc(sizeof(struct evpaxos_config), GFP_KERNEL);
	memset(c, 0, sizeof(struct evpaxos_config));

	line = "acceptor 0 127.0.0.3 3003";
	parse_line(c, line);
	line = "acceptor 1 127.0.0.3 4003";
	parse_line(c, line);
	line = "acceptor 2 127.0.0.3 5003";
	parse_line(c, line);
	line = "proposer 0 127.0.0.2 3002";
	parse_line(c, line);
	line = "proposer 1 127.0.0.2 4002";
	parse_line(c, line);
	line = "proposer 2 127.0.0.2 5002";
	parse_line(c, line);
	// line = "learner 0 127.0.0.4 3004"; 
	// parse_line(c, line);
	// line = "learner 1 127.0.0.4 4004";
	// parse_line(c, line);
	// line = "learner 2 127.0.0.4 5004";
	// parse_line(c, line);


	return c;

}

void
evpaxos_config_free(struct evpaxos_config* config)
{
	int i;
	for (i = 0; i < config->proposers_count; ++i)
		address_free(&config->proposers[i]);
	for (i = 0; i < config->acceptors_count; ++i)
		address_free(&config->acceptors[i]);
	kfree(config);
}

struct sockaddr_in
evpaxos_proposer_address(struct evpaxos_config* config, int i)
{
	return address_to_sockaddr(&config->proposers[i]);
}

int
evpaxos_proposer_listen_port(struct evpaxos_config* config, int i)
{
	return config->proposers[i].port;
}

int
evpaxos_acceptor_count(struct evpaxos_config* config)
{
	return config->acceptors_count;
}

int
evpaxos_proposer_count(struct evpaxos_config* config)
{
	return config->proposers_count;
}

struct sockaddr_in
evpaxos_acceptor_address(struct evpaxos_config* config, int i)
{
	return address_to_sockaddr(&config->acceptors[i]);
}

// struct sockaddr_in
// evpaxos_learner_address(struct evpaxos_config* config, int i)
// {
// 	return address_to_sockaddr(&config->learners[i]);
// }

int
evpaxos_acceptor_listen_port(struct evpaxos_config* config, int i)
{
	return config->acceptors[i].port;
}

static char*
strtrim(char* string)
{
	char *s, *t;
	for (s = string; isspace(*s); s++)
		;
	if (*s == 0)
		return s;
	t = s + strlen(s) - 1;
	while (t > s && isspace(*t))
		t--;
	*++t = '\0';
	return s;
}

// static int
// parse_bytes(char* str, size_t* bytes)
// {
// 	char* end;
// 	int errno = 0; /* To distinguish strtoll's return value 0 */
// 	*bytes = kstrtoull(str, 10, end);
// 	if (errno != 0) return 0;
// 	while (isspace(*end)) end++;
// 	if (*end != '\0') {
// 		if (strcasecmp(end, "kb") == 0) *bytes *= 1024;
// 		else if (strcasecmp(end, "mb") == 0) *bytes *= 1024 * 1024;
// 		else if (strcasecmp(end, "gb") == 0) *bytes *= 1024 * 1024 * 1024;
// 		else return 0;
// 	}
// 	return 1;
// }

static int
parse_boolean(char* str, int* boolean)
{
	if (str == NULL) return 0;
	if (strcasecmp(str, "yes") == 0) {
		*boolean = 1;
		return 1;
	}
	if (strcasecmp(str, "no") == 0) {
		*boolean = 0;
		return 1;
	}
	return 0;
}

static int
parse_integer(char* str, int* integer)
{
	long n;
	if (str == NULL) return 0;
	if (kstrtol(str, 10, &n) != 0) return 0;
	*integer = n;
	return 1;
}

static int
parse_string(char* str, char** string)
{
	if (str == NULL || str[0] == '\0' || str[0] == '\n')
		return 0;
	*string = kmalloc(strlen(str) + 1, GFP_KERNEL);
	strcpy(*string, str);
	// *string = strdup(str);
	return 1;
}

static int
parse_address(char* str, struct address* addr)
{
	int id;
	int port;
	char address[128];
	int rv = sscanf(str, "%d %s %d", &id, address, &port);
	if (rv == 3) {
		address_init(addr, address, port);
		return 1;
	}
	return 0;
}

static int
parse_verbosity(char* str, paxos_log_level* verbosity)
{
	if (strcasecmp(str, "quiet") == 0) *verbosity = PAXOS_LOG_QUIET;
	else if (strcasecmp(str, "error") == 0) *verbosity = PAXOS_LOG_ERROR;
	else if (strcasecmp(str, "info") == 0) *verbosity = PAXOS_LOG_INFO;
	else if (strcasecmp(str, "debug") == 0) *verbosity = PAXOS_LOG_DEBUG;
	else return 0;
	return 1;
}

// static int
// parse_backend(char* str, paxos_storage_backend* backend)
// {
// 	if (strcasecmp(str, "memory") == 0) *backend = PAXOS_MEM_STORAGE;
// 	else if (strcasecmp(str, "lmdb") == 0) *backend = PAXOS_LMDB_STORAGE;
// 	else return 0;
// 	return 1;
// }

static struct option*
lookup_option(char* opt)
{
	int i = 0;
	while (options[i].name != NULL) {
		if (strcasecmp(options[i].name, opt) == 0)
			return &options[i];
		i++;
	}
	return NULL;
}

static int
parse_line(struct evpaxos_config* c, char* line)
{
	int rv = 0;
	char* tok;
	char* sep = " ";
	struct option* opt;

	line = strtrim(line);
	tok = strsep(&line, sep);

	if (strcasecmp(tok, "a") == 0 || strcasecmp(tok, "acceptor") == 0) {
		if (c->acceptors_count >= MAX_N_OF_PROPOSERS) {
			paxos_log_error("Number of acceptors exceded maximum of: %d\n",
				MAX_N_OF_PROPOSERS);
			return 0;
		}
		struct address* addr = &c->acceptors[c->acceptors_count++];
		return parse_address(line, addr);
	}

	if (strcasecmp(tok, "p") == 0 || strcasecmp(tok, "proposer") == 0) {
		if (c->proposers_count >= MAX_N_OF_PROPOSERS) {
			paxos_log_error("Number of proposers exceded maximum of: %d\n",
				MAX_N_OF_PROPOSERS);
			return 0;
		}
		struct address* addr = &c->proposers[c->proposers_count++];
		return parse_address(line, addr);
	}

	// if (strcasecmp(tok, "l") == 0 || strcasecmp(tok, "learner") == 0) {
	// 	if (c->learners_count >= MAX_N_OF_LEARNERS) {
	// 		paxos_log_error("Number of learners exceded maximum of: %d\n",
	// 			MAX_N_OF_LEARNERS);
	// 		return 0;
	// 	}
	// 	struct address* addr = &c->learners[c->learners_count++];
	// 	return parse_address(line, addr);
	// }

	if (strcasecmp(tok, "r") == 0 || strcasecmp(tok, "replica") == 0) {
		if (c->proposers_count >= MAX_N_OF_PROPOSERS ||
			c->acceptors_count >= MAX_N_OF_PROPOSERS ) {
				paxos_log_error("Number of replicas exceded maximum of: %d\n",
					MAX_N_OF_PROPOSERS);
				return 0;
		}
		struct address* pro_addr = &c->proposers[c->proposers_count++];
		struct address* acc_addr = &c->acceptors[c->acceptors_count++];
		rv = parse_address(line, pro_addr);
		address_copy(pro_addr, acc_addr);
		return rv;
	}

	line = strtrim(line);
	opt = lookup_option(tok);
	if (opt == NULL)
		return 0;

	switch (opt->type) {
		case option_boolean:
			rv = parse_boolean(line, opt->value);
			if (rv == 0) paxos_log_error("Expected 'yes' or 'no'\n");
			break;
		case option_integer:
			rv = parse_integer(line, opt->value);
			if (rv == 0) paxos_log_error("Expected number\n");
			break;
		case option_string:
			rv = parse_string(line, opt->value);
			if (rv == 0) paxos_log_error("Expected string\n");
			break;
		case option_verbosity:
			rv = parse_verbosity(line, opt->value);
			if (rv == 0) paxos_log_error("Expected quiet, error, info, or debug\n");
			break;
		// case option_backend:
		// 	rv = parse_backend(line, opt->value);
		// 	if (rv == 0) paxos_log_error("Expected memory or lmdb\n");
		// 	break;
		// case option_bytes:
		// 	rv = parse_bytes(line, opt->value);
		// 	if (rv == 0) paxos_log_error("Expected number of bytes.\n");
	}

	return rv;
}

static void
address_init(struct address* a, char* addr, int port)
{
	a->addr = kmalloc(strlen(addr) + 1, GFP_KERNEL);
	strcpy(a->addr, addr);
	// a->addr = strdup(addr);
	a->port = port;
}

static void
address_free(struct address* a)
{
	kfree(a->addr);
}

static void
address_copy(struct address* src, struct address* dst)
{
	address_init(dst, src->addr, src->port);
}

static struct sockaddr_in
address_to_sockaddr(struct address* a)
{
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(a->port);
	addr.sin_addr.s_addr = inet_addr(a->addr);
	return addr;
}
