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

#ifndef _PEERS_H_
#define _PEERS_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include "evpaxos.h"
#include "paxos.h"
#include "paxos_types.h"

  struct peer;
  struct peers;

  typedef void (*peer_cb)(paxos_message* m, void* arg, eth_address* src);
  typedef void (*peer_iter_cb)(struct net_device* dev, struct peer* p,
                               void* arg);

  struct peers* peers_new(struct evpaxos_config* config, int id, char* if_name);
  void          peers_free(struct peers* p);
  int           peers_count(struct peers* p);
  eth_address*  get_addr(struct peer* p);
  void peers_foreach_acceptor(struct peers* p, peer_iter_cb cb, void* arg);
  void peers_foreach_client(struct peers* p, peer_iter_cb cb, void* arg);
  struct peer* peers_get_acceptor(struct peers* p, int id);
  void add_acceptors_from_config(struct peers* p, struct evpaxos_config* conf);
  void printall(struct peers* p, char* name);
  int  add_or_update_client(eth_address* addr, struct peers* p);
  int  peer_get_id(struct peer* p);
  void peer_send_del(struct net_device* dev, struct peer* p, void* arg);
  struct net_device* get_dev(struct peers* p);
  int                peers_missing_ok(struct peers* p);
  void               peers_update_ok(struct peers* p, eth_address* addr);
  void               peers_delete_learner(struct peers* p, eth_address* addr);
  void               peers_subscribe(struct peers* p);
  void peers_add_subscription(struct peers* p, paxos_message_type type,
                              peer_cb cb, void* arg);
#ifdef __cplusplus
}
#endif

#endif
