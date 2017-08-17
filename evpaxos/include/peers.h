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
extern "C" {
#endif

#include "paxos.h"
#include "evpaxos.h"
#include "paxos_types.h"
#include "kernel_udp.h"

struct peer;
struct peers;

typedef void (*peer_cb)(struct peer* p, paxos_message* m, void* arg);
typedef void (*peer_iter_cb)(struct peer* p, void* arg);

struct peers* peers_new(struct sockaddr_in * send_addr, struct evpaxos_config* config, int id);
void peers_free(struct peers* p);
int peers_count(struct peers* p);
int peers_sock_init(struct peers* p, udp_service * k);
int peers_listen(struct peers* p, udp_service * k);
void peers_subscribe(struct peers* p, paxos_message_type type, peer_cb cb, void* arg);
void peers_foreach_acceptor(struct peers* p, peer_iter_cb cb, void* arg);
void peers_foreach_client(struct peers* p, peer_iter_cb cb, void* arg);
struct peer* peers_get_acceptor(struct peers* p, int id);
void add_proposers_from_config(int myid, struct peers * p);
void add_acceptors_from_config(int myid, struct peers * p);
struct socket * get_send_socket(struct peer * p);
// struct socket * get_rcv_socket(struct peer * p);
struct sockaddr_in * get_sockaddr(struct peer * p);
struct peer * get_me_send(struct peers * p);
// struct peer * get_me_rcv(struct peers * p);
int peer_get_id(struct peer* p);
void printall(struct peers * p);

#ifdef __cplusplus
}
#endif

#endif
