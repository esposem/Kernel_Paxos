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

#include "evpaxos.h"
#include "kfile.h"
#include "paxos.h"

#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/inet.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <net/sock.h>

struct address
{
  char* addr;
  int   port;
};

struct evpaxos_config
{
  int          proposers_count;
  int          acceptors_count;
  eth_address* proposers[MAX_N_OF_PROPOSERS];
  eth_address* acceptors[MAX_N_OF_PROPOSERS];
};

enum option_type
{
  option_boolean,
  option_integer,
  option_string,
  option_verbosity,
};

struct option
{
  const char*      name;
  void*            value;
  enum option_type type;
};

struct option options[] = {
  { "verbosity", &paxos_config.verbosity, option_verbosity },
  { "tcp-nodelay", &paxos_config.tcp_nodelay, option_boolean },
  { "learner-catch-up", &paxos_config.learner_catch_up, option_boolean },
  { "proposer-timeout", &paxos_config.proposer_timeout, option_integer },
  { "proposer-preexec-window", &paxos_config.proposer_preexec_window,
    option_integer },
  { "acceptor-trash-files", &paxos_config.trash_files, option_boolean },
  { 0 }
};

static int  parse_line(struct evpaxos_config* c, char* line);
static int  str_to_mac(const char* str, eth_address* daddr);
static void address_free(eth_address* a);

unsigned int
inet_addr(char* str)
{
  int  a, b, c, d;
  char arr[4];
  sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d);
  arr[0] = a;
  arr[1] = b;
  arr[2] = c;
  arr[3] = d;
  return *(unsigned int*)arr;
}

struct evpaxos_config*
evpaxos_config_read(char* name)
{
  struct file*           f;
  struct evpaxos_config* c = NULL;
  int                    offset = 0;
  int                    read = 0;
  int                    line_pos = 0;

  c = pmalloc(sizeof(struct evpaxos_config));
  if (c == NULL)
    return NULL;
  memset(c, 0, sizeof(struct evpaxos_config));

  int   SIZE_LINE = 512;
  char* line = pmalloc(SIZE_LINE);
  if (!line) {
    return NULL;
  }
  memset(line, 0, SIZE_LINE);

  f = file_open(name, O_RDONLY, S_IRUSR | S_IRGRP);
  if (!f) {
    return NULL;
  }

  // TODO LATER put in file.h
  while ((read = file_read(f, offset, &(line[line_pos]), 1)) != 0) {
    offset += read;

    // if new line
    if (line[line_pos] == '\n') {
      line[line_pos] = '\0';
      parse_line(c, line);
      memset(line, 0, SIZE_LINE);
      line_pos = 0;
      continue;
    }

    line_pos++;

    // if comment or line too long
    if (line_pos == SIZE_LINE || line[line_pos - 1] == '#') {
      line_pos = 0;
      char c;
      // skip rest of the line
      while ((read = file_read(f, offset, &c, 1)) != 0) {
        offset += read;
        if (c == '\n') {
          break;
        }
      }
    }
  }

  // safety check in case the file does not end with \n
  if (line_pos > 0) {
    line[line_pos] = '\0';
    parse_line(c, line);
  }

  pfree(line);
  file_close(f);

  return c;
}

void
evpaxos_config_free(struct evpaxos_config* config)
{
  int i;
  for (i = 0; i < config->proposers_count; ++i)
    address_free(config->proposers[i]);
  for (i = 0; i < config->acceptors_count; ++i)
    address_free(config->acceptors[i]);
  pfree(config);
}

eth_address*
evpaxos_proposer_address(struct evpaxos_config* config, int i)
{
  if (i < evpaxos_proposer_count(config))
    return config->proposers[i];
  return NULL;
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

eth_address*
evpaxos_acceptor_address(struct evpaxos_config* config, int i)
{
  if (i < evpaxos_acceptor_count(config))
    return config->acceptors[i];
  return NULL;
}

// a string "   ciao   " becomes "ciao"
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

static int
parse_boolean(char* str, int* boolean)
{
  if (str == NULL)
    return 0;
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
  if (str == NULL)
    return 0;
  if (kstrtol(str, 10, &n) != 0)
    return 0;
  *integer = n;
  return 1;
}

static int
parse_string(char* str, char** string)
{
  if (str == NULL || str[0] == '\0' || str[0] == '\n')
    return 0;
  *string = pmalloc(strlen(str) + 1);
  if (*string)
    strcpy(*string, str);
  return 1;
}

static int
str_to_mac(const char* str, eth_address* daddr)
{
  int values[6], i;
  if (6 == sscanf(str, "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2],
                  &values[3], &values[4], &values[5])) {
    /* convert to uint8_t */
    for (i = 0; i < 6; ++i)
      daddr[i] = (uint8_t)values[i];
    return 1;
  }
  return 0;
}

static int
parse_address(char* str, eth_address* addr)
{
  int  id;
  char address[128];
  int  rv = sscanf(str, "%d %s", &id, address);
  if (rv == 2) {
    str_to_mac(address, addr);
    return 1;
  }
  return 0;
}

static int
parse_verbosity(char* str, paxos_log_level* verbosity)
{
  if (strcasecmp(str, "quiet") == 0)
    *verbosity = PAXOS_LOG_QUIET;
  else if (strcasecmp(str, "error") == 0)
    *verbosity = PAXOS_LOG_ERROR;
  else if (strcasecmp(str, "info") == 0)
    *verbosity = PAXOS_LOG_INFO;
  else if (strcasecmp(str, "debug") == 0)
    *verbosity = PAXOS_LOG_DEBUG;
  else
    return 0;
  return 1;
}

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
  int            rv = 0;
  char*          tok;
  char*          sep = " ";
  struct option* opt;

  line = strtrim(line);
  tok = strsep(&line, sep);

  if (strcasecmp(tok, "a") == 0 || strcasecmp(tok, "acceptor") == 0) {
    if (c->acceptors_count >= MAX_N_OF_PROPOSERS) {
      paxos_log_error("Number of acceptors exceded maximum of: %d\n",
                      MAX_N_OF_PROPOSERS);
      return 0;
    }
    c->acceptors[c->acceptors_count] = pmalloc(ETH_ALEN);
    eth_address* addr = c->acceptors[c->acceptors_count++];

    return parse_address(line, addr);
  }

  if (strcasecmp(tok, "p") == 0 || strcasecmp(tok, "proposer") == 0) {
    if (c->proposers_count >= MAX_N_OF_PROPOSERS) {
      paxos_log_error("Number of proposers exceded maximum of: %d\n",
                      MAX_N_OF_PROPOSERS);
      return 0;
    }
    c->proposers[c->proposers_count] = pmalloc(ETH_ALEN);
    eth_address* addr = c->proposers[c->proposers_count++];
    return parse_address(line, addr);
  }

  if (strcasecmp(tok, "r") == 0 || strcasecmp(tok, "replica") == 0) {
    if (c->proposers_count >= MAX_N_OF_PROPOSERS ||
        c->acceptors_count >= MAX_N_OF_PROPOSERS) {
      paxos_log_error("Number of replicas exceded maximum of: %d\n",
                      MAX_N_OF_PROPOSERS);
      return 0;
    }
    c->acceptors[c->acceptors_count] = pmalloc(ETH_ALEN);
    eth_address* acc_addr = c->acceptors[c->acceptors_count++];
    c->proposers[c->proposers_count] = pmalloc(ETH_ALEN);
    eth_address* pro_addr = c->proposers[c->proposers_count++];
    rv = parse_address(line, pro_addr);
    memcpy(acc_addr, pro_addr, ETH_ALEN);
    return rv;
  }

  line = strtrim(line);
  opt = lookup_option(tok);
  if (opt == NULL)
    return 0;

  switch (opt->type) {
    case option_boolean:
      rv = parse_boolean(line, opt->value);
      if (rv == 0)
        paxos_log_error("Expected 'yes' or 'no'\n");
      break;
    case option_integer:
      rv = parse_integer(line, opt->value);
      if (rv == 0)
        paxos_log_error("Expected number\n");
      break;
    case option_string:
      rv = parse_string(line, opt->value);
      if (rv == 0)
        paxos_log_error("Expected string\n");
      break;
    case option_verbosity:
      rv = parse_verbosity(line, opt->value);
      if (rv == 0)
        paxos_log_error("Expected quiet, error, info, or debug\n");
      break;
  }

  return rv;
}

static void
address_free(eth_address* a)
{
  pfree(a);
}
