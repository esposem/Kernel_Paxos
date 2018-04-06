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


#ifndef _TCP_SENDBUF_H_
#define _TCP_SENDBUF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "paxos_types.h"
#include "common.h"
#include "eth.h"
#include <linux/udp.h>

void send_paxos_message(struct net_device *dev, eth_address *addr,
                        paxos_message *msg);
void send_paxos_prepare(struct net_device *dev, eth_address *addr,
                        paxos_prepare *pp);
void send_paxos_promise(struct net_device *dev, eth_address *addr,
                        paxos_promise *p);
void send_paxos_accept(struct net_device *dev, eth_address *addr, paxos_accept *pa);
void send_paxos_accepted(struct net_device *dev, eth_address *addr,
                         paxos_accepted *p);
void send_paxos_preempted(struct net_device *dev, eth_address *addr,
                          paxos_preempted *p);
void send_paxos_repeat(struct net_device *dev, eth_address *addr,
                       paxos_repeat *p);
void send_paxos_trim(struct net_device *dev, eth_address *addr, paxos_trim *t);
int recv_paxos_message(paxos_message *out, paxos_message_type p, char *data,
                       size_t size);
void send_paxos_learner_hi(struct net_device *dev, eth_address *addr);
void send_paxos_learner_del(struct net_device *dev, eth_address *addr);
void send_paxos_acceptor_ok(struct net_device *dev, eth_address *addr);
#ifdef __cplusplus
}
#endif

#endif
