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

#include "message.h"
#include "eth.h"
#include "paxos.h"
#include "paxos_types_pack.h"

void
send_paxos_message(struct net_device* dev, eth_address* addr,
                   paxos_message* msg)
{
  msgpack_packer packer[ETH_DATA_LEN];
  long           size_msg = msgpack_pack_paxos_message(packer, msg);

  eth_send(dev, addr, (uint16_t)msg->type, packer, size_msg);
}

void
send_paxos_learner_hi(struct net_device* dev, eth_address* addr)
{
  paxos_message msg = { .type = PAXOS_LEARNER_HI };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Learner: Send hi to the acceptors");
}

void
send_paxos_acceptor_ok(struct net_device* dev, eth_address* addr)
{
  paxos_message msg = { .type = PAXOS_ACCEPTOR_OK };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Acceptor: Send ok to the learner");
}

void
send_paxos_learner_del(struct net_device* dev, eth_address* addr)
{
  paxos_message msg = { .type = PAXOS_LEARNER_DEL };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Learner: Send del to the acceptors");
}

void
send_paxos_prepare(struct net_device* dev, eth_address* addr, paxos_prepare* pp)
{

  paxos_message msg = { .type = PAXOS_PREPARE, .u.prepare = *pp };

  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Proposer: Send prepare for iid %d ballot %d", pp->iid,
                  pp->ballot);
}

void
send_paxos_promise(struct net_device* dev, eth_address* addr, paxos_promise* p)
{
  paxos_message msg = { .type = PAXOS_PROMISE, .u.promise = *p };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Acceptor: Send promise for iid %d ballot %d", p->iid,
                  p->ballot);
}

void
send_paxos_accept(struct net_device* dev, eth_address* addr, paxos_accept* pa)
{
  paxos_message msg = { .type = PAXOS_ACCEPT, .u.accept = *pa };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Proposer: Send accept for iid %d ballot %d", pa->iid,
                  pa->ballot);
}

void
send_paxos_accepted(struct net_device* dev, eth_address* addr,
                    paxos_accepted* p)
{
  paxos_message msg = { .type = PAXOS_ACCEPTED, .u.accepted = *p };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Acceptor: Send accepted for inst %d ballot %d", p->iid,
                  p->ballot);
}

void
send_paxos_preempted(struct net_device* dev, eth_address* addr,
                     paxos_preempted* p)
{
  paxos_message msg = { .type = PAXOS_PREEMPTED, .u.preempted = *p };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Acceptor Send preempted for inst %d ballot %d", p->iid,
                  p->ballot);
}

void
send_paxos_repeat(struct net_device* dev, eth_address* addr, paxos_repeat* p)
{
  paxos_message msg = { .type = PAXOS_REPEAT, .u.repeat = *p };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Learner: Send repeat for inst %d-%d", p->from, p->to);
}

void
send_paxos_trim(struct net_device* dev, eth_address* addr, paxos_trim* t)
{
  paxos_message msg = { .type = PAXOS_TRIM, .u.trim = *t };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Learner: Send trim for inst %d", t->iid);
}

void
paxos_submit(struct net_device* dev, eth_address* addr, char* data, int size)
{
  paxos_message msg = { .type = PAXOS_CLIENT_VALUE,
                        .u.client_value.value.paxos_value_len = size,
                        .u.client_value.value.paxos_value_val = data };
  send_paxos_message(dev, addr, &msg);
  paxos_log_debug("Client: Sent client value size data %d", size);
}

int
recv_paxos_message(paxos_message* out, paxos_message_type p, char* data,
                   size_t size)
{
  return msgpack_unpack_paxos_message(out, p, data, size);
}
