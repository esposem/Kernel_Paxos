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

#include "evpaxos_internal.h"
#include "message.h"
#include <linux/slab.h>

struct evpaxos_replica
{
  struct peers*      peers;
  struct evlearner*  learner;
  struct evproposer* proposer;
  struct evacceptor* acceptor;
  deliver_function   deliver;
  void*              arg;
};

static void
evpaxos_replica_deliver(unsigned iid, char* value, size_t size, void* arg)
{
  struct evpaxos_replica* r = arg;
  evproposer_set_instance_id(r->proposer, iid);
  if (r->deliver) {
    r->deliver(iid, value, size, r->arg);
  }
}

struct evpaxos_replica*
evpaxos_replica_init(int id, deliver_function f, void* arg, char* if_name,
                     char* path)
{
  struct evpaxos_replica* replica;
  struct evpaxos_config*  config;

  config = evpaxos_config_read(path);
  if (config == NULL) {
    return NULL;
  }

  replica = pmalloc(sizeof(struct evpaxos_replica));
  if (replica == NULL) {
    return NULL;
  }

  replica->peers = peers_new(config, id, if_name);
  if (replica->peers == NULL)
    return NULL;
  add_acceptors_from_config(replica->peers, config);
  printall(replica->peers, "Replica");
  replica->deliver = f;
  replica->arg = arg;
  replica->acceptor = evacceptor_init_internal(id, config, replica->peers);
  // replica->learner = NULL;
  replica->learner = evlearner_init_internal(config, replica->peers,
                                             evpaxos_replica_deliver, replica);
  replica->proposer = evproposer_init_internal(id, config, replica->peers);
  peers_subscribe(replica->peers);
  evlearner_send_hi(replica->peers);
  evproposer_preexec_once(replica->proposer);
  evpaxos_config_free(config);
  return replica;
}

void
evpaxos_replica_free(struct evpaxos_replica* r)
{
  printall(r->peers, "REPLICA");
  if (r->learner)
    evlearner_free_internal(r->learner);
  if (r->proposer)
    evproposer_free_internal(r->proposer);
  if (r->acceptor)
    evacceptor_free_internal(r->acceptor);

  peers_free(r->peers);
  pfree(r);
}

void
evpaxos_replica_set_instance_id(struct evpaxos_replica* r, unsigned iid)
{
  if (r->learner)
    evlearner_set_instance_id(r->learner, iid);
  evproposer_set_instance_id(r->proposer, iid);
}

static void
peer_send_trim(struct net_device* dev, struct peer* p, void* arg)
{
  send_paxos_trim(dev, get_addr(p), arg);
}

void
evpaxos_replica_send_trim(struct evpaxos_replica* r, unsigned iid)
{
  paxos_trim trim = { iid };
  peers_foreach_acceptor(r->peers, peer_send_trim, &trim);
}

// void evpaxos_replica_submit(struct evpaxos_replica *r, char *value, int size)
// {
//   int i;
//   struct peer *p;
//   for (i = 0; i < peers_count(r->peers); ++i) {
//     p = peers_get_acceptor(r->peers, i);
//     paxos_submit(get_dev(r->peers), p->, value, size);
//     return;
//   }
// }

int
evpaxos_replica_count(struct evpaxos_replica* r)
{
  return peers_count(r->peers);
}
