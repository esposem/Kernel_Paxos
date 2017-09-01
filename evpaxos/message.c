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


#include "paxos.h"
#include "message.h"
#include "paxos_types_pack.h"

#include <linux/slab.h>
#include "kernel_udp.h"


void
send_paxos_message(struct socket * s, struct sockaddr_in * bev, paxos_message* msg, char * name)
{
	msgpack_packer * packer;
	long size_msg = msgpack_pack_paxos_message(&packer, msg);
	udp_server_send(s, bev, (unsigned char *) packer, size_msg, name);
	kfree(packer);
}

void
send_paxos_learner_hi(struct socket * s,struct sockaddr_in* bev, paxos_learner_hi* p)
{
	paxos_message msg = {
		.type = PAXOS_LEARNER_HI,
		.u.learner_hi.value.paxos_value_len=0,
	 	.u.learner_hi.value.paxos_value_val = NULL};
	send_paxos_message(s, bev, &msg, "Learner:");
	// printk(KERN_INFO "Learner: Send hi to the acceptors");
}

void
send_paxos_learner_del(struct socket * s,struct sockaddr_in* bev, paxos_learner_del* p)
{
	paxos_message msg = {
		.type = PAXOS_LEARNER_DEL,
		.u.learner_del.value.paxos_value_len=0,
	 	.u.learner_del.value.paxos_value_val = NULL};
	send_paxos_message(s, bev, &msg, "Learner:");
	// printk(KERN_INFO "Learner: Send del to the acceptors");
}

void
send_paxos_prepare(struct socket * s,struct sockaddr_in* bev, void * pa)
{
	struct paxos_prepare * p = (paxos_prepare *) pa;
	paxos_message msg = {
		.type = PAXOS_PREPARE,
		.u.prepare = *p };
	send_paxos_message(s, bev, &msg, "Proposer:");
	// printk(KERN_INFO "Proposer: Send prepare for iid %d ballot %d", p->iid, p->ballot);
}

void
send_paxos_promise(struct socket * s, struct sockaddr_in* bev, paxos_promise* p)
{
	paxos_message msg = {
		.type = PAXOS_PROMISE,
		.u.promise = *p };
	send_paxos_message(s, bev, &msg, "Acceptor:");
	// printk(KERN_INFO "Acceptor: Send promise for iid %d ballot %d", p->iid, p->ballot);
}

void
send_paxos_accept(struct socket * s, struct sockaddr_in* bev, void * pa)
{
	struct paxos_accept * p = (paxos_accept *) pa;
	paxos_message msg = {
		.type = PAXOS_ACCEPT,
		.u.accept = *p };
	send_paxos_message(s, bev, &msg, "Proposer:");
	// printk(KERN_INFO "Proposer: Send accept for iid %d ballot %d", p->iid, p->ballot);
}

void
send_paxos_accepted(struct socket * s, struct sockaddr_in* bev, void * pa)
{
	struct paxos_accepted * p = (paxos_accepted *) pa;
	paxos_message msg = {
		.type = PAXOS_ACCEPTED,
		.u.accepted = *p };
	send_paxos_message(s, bev, &msg, "Acceptor:");
	// printk(KERN_INFO "Acceptor: Send accepted for inst %d ballot %d", p->iid, p->ballot);
}

void
send_paxos_preempted(struct socket * s, struct sockaddr_in* bev, paxos_preempted* p)
{
	paxos_message msg = {
		.type = PAXOS_PREEMPTED,
		.u.preempted = *p };
	send_paxos_message(s,bev, &msg, "Acceptor");
	// printk(KERN_INFO "Acceptor Send preempted for inst %d ballot %d", p->iid, p->ballot);
}

void
send_paxos_repeat(struct socket * s, struct sockaddr_in* bev, paxos_repeat* p)
{
	paxos_message msg = {
		.type = PAXOS_REPEAT,
		.u.repeat = *p };
	send_paxos_message(s,bev, &msg, "Learner:");
	// printk(KERN_INFO "Learner: Send repeat for inst %d-%d", p->from, p->to);
}

void
send_paxos_trim(struct socket * s, struct sockaddr_in* bev, paxos_trim* t)
{
	paxos_message msg = {
		.type = PAXOS_TRIM,
		.u.trim = *t };
	send_paxos_message(s,bev, &msg, "Learner:");
	// printk(KERN_INFO "Learner: Send trim for inst %d", t->iid);
}

void
paxos_submit(struct socket * s, struct sockaddr_in* bev, char* data, int size)
{
	paxos_message msg = {
		.type = PAXOS_CLIENT_VALUE,
		.u.client_value.value.paxos_value_len = size,
		.u.client_value.value.paxos_value_val = data };
	send_paxos_message(s, bev, &msg, "Client:");
	// printk(KERN_INFO "Client: Sent client value");

}

int recv_paxos_message(char * data, paxos_message* out, int size)
{
	return msgpack_unpack_paxos_message(data, out, size);
}
