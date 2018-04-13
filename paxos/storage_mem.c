/*
 * Copyright (c) 2013-2014, University of Lugano
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

#include "common.h"
#include "storage.h"
#include <linux/if_ether.h>
#include <linux/slab.h>

// TODO use vmalloc

static const int MAX_SIZE = 2000;

typedef struct store
{
  paxos_accepted msg;
  char           data[ETH_DATA_LEN - sizeof(paxos_accepted)];
} store_t;

struct mem_storage
{
  store_t* st;
  iid_t    trim_iid; // just for compatibility
};

static struct mem_storage*
mem_storage_new(int acceptor_id)
{
  struct mem_storage* s = pmalloc(sizeof(struct mem_storage));

  s->st = pmalloc(MAX_SIZE * sizeof(struct store));
  memset(s->st, 0, MAX_SIZE * sizeof(struct store));
  s->trim_iid = 0;
  return s;
}

static int
mem_storage_open(void* handle)
{
  return 0;
}

static void
mem_storage_close(void* handle)
{
  struct mem_storage* s = handle;
  if (s) {
    pfree(s->st);
    pfree(s);
  }
}

static int
mem_storage_tx_begin(void* handle)
{
  return 0;
}

static int
mem_storage_tx_commit(void* handle)
{
  return 0;
}

static void
mem_storage_tx_abort(void* handle)
{}

static int
mem_storage_get(void* handle, iid_t iid, paxos_accepted* out)
{
  struct mem_storage* s = handle;
  int                 idx = iid % MAX_SIZE;

  if (s->st[idx].msg.iid == iid) {
    memcpy(out, &s->st[idx].msg, sizeof(paxos_accepted));
    if (out->value.paxos_value_len > 0) {
      out->value.paxos_value_val = pmalloc(out->value.paxos_value_len);
      memcpy(out->value.paxos_value_val, s->st[idx].data,
             out->value.paxos_value_len);
    }
    return 1;
  }

  return 0;
}

static int
mem_storage_put(void* handle, paxos_accepted* acc)
{
  struct mem_storage* s = handle;
  int                 idx = acc->iid % MAX_SIZE;

  memcpy(&s->st[idx].msg, acc, sizeof(paxos_accepted));
  if (s->st[idx].msg.value.paxos_value_len > sizeof(s->st[idx].data)) {
    LOG_ERROR("Data will be truncated.");
    s->st[idx].msg.value.paxos_value_len = sizeof(s->st[idx].data);
  }
  memcpy(s->st[idx].data, acc->value.paxos_value_val,
         s->st[idx].msg.value.paxos_value_len);
  return 0;
}

static int
mem_storage_trim(void* handle, iid_t iid)
{
  struct mem_storage* s = handle;
  s->trim_iid = iid;
  return 0;
}

static iid_t
mem_storage_get_trim_instance(void* handle)
{
  struct mem_storage* s = handle;
  return s->trim_iid;
}

void
storage_init_mem(struct storage* s, int acceptor_id)
{
  s->handle = mem_storage_new(acceptor_id);
  s->api.open = mem_storage_open;
  s->api.close = mem_storage_close;
  s->api.tx_begin = mem_storage_tx_begin;
  s->api.tx_commit = mem_storage_tx_commit;
  s->api.tx_abort = mem_storage_tx_abort;
  s->api.get = mem_storage_get;
  s->api.put = mem_storage_put;
  s->api.trim = mem_storage_trim;
  s->api.get_trim_instance = mem_storage_get_trim_instance;
}