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


#include "evpaxos.h"
#include "acceptor.h"
#include "message.h"
//
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/udp.h>
#include "peers.h"

struct evacceptor
{
	struct peers* peers;
	struct acceptor* state;
	struct timer_list timer_ev;
	struct timeval timer_tv;
	unsigned long arr[2];
};


void paxos_acceptor_listen(udp_service * k, struct evacceptor * ev){
	peers_listen(ev->peers, k);
}

static void
peer_send_paxos_message(struct peer* p, void* arg)
{
	send_paxos_message(get_send_socket(p), get_sockaddr(p), arg);
}

/*
	Received a prepare request (phase 1a).
*/
static void
evacceptor_handle_prepare(struct peer* p, paxos_message* msg, void* arg)
{
	paxos_message out;
	paxos_prepare* prepare = &msg->u.prepare;
	struct evacceptor* a = (struct evacceptor*)arg;
	printk(KERN_INFO "Acceptor: Received PREPARE for iid %d, ballot %d",
		prepare->iid, prepare->ballot);
	if (acceptor_receive_prepare(a->state, prepare, &out) != 0) {
		send_paxos_message( get_send_socket(p),get_sockaddr(p), &out);
		paxos_message_destroy(&out);
		printk(KERN_INFO "Acceptor: sent promise for iid %d", prepare->iid);
	}
}

/*
	Received a accept request (phase 2a).
*/
static void
evacceptor_handle_accept(struct peer* p, paxos_message* msg, void* arg)
{
	paxos_message out;
	paxos_accept* accept = &msg->u.accept;
	struct evacceptor* a = (struct evacceptor*)arg;
	printk(KERN_INFO "Acceptor: Received ACCEPT REQUEST for iid %d ballot %d",
		accept->iid, accept->ballot);
	if (acceptor_receive_accept(a->state, accept, &out) != 0) {
		if (out.type == PAXOS_ACCEPTED) {
			printk(KERN_INFO "Acceptor: Sent ACCEPTED to all proposers and learners");
			peers_foreach_client(a->peers, peer_send_paxos_message, &out);
		} else if (out.type == PAXOS_PREEMPTED) {
			printk(KERN_INFO "Acceptor: Sent PREEMPTED to all proposers ");
			send_paxos_message(get_send_socket(p), get_sockaddr(p), &out);
		}
		paxos_message_destroy(&out);
	}
}

static void
evacceptor_handle_repeat(struct peer* p, paxos_message* msg, void* arg)
{
	iid_t iid;
	paxos_accepted accepted;
	paxos_repeat* repeat = &msg->u.repeat;
	struct evacceptor* a = (struct evacceptor*)arg;
	printk(KERN_INFO "Acceptor: Handle repeat for iids %d-%d", repeat->from, repeat->to);
	for (iid = repeat->from; iid <= repeat->to; ++iid) {
		if (acceptor_receive_repeat(a->state, iid, &accepted)) {
			printk(KERN_INFO "Acceptor: sent a repeated PAXOS_ACCEPTED to proposer");
			send_paxos_accepted(get_send_socket(p), get_sockaddr(p), &accepted);
			paxos_accepted_destroy(&accepted);
		}
	}
}

static void
evacceptor_handle_trim(struct peer* p, paxos_message* msg, void* arg)
{
	printk(KERN_INFO "Acceptor: Received PAXOS_TRIM. Deleting the old instances");
	paxos_trim* trim = &msg->u.trim;
	struct evacceptor* a = (struct evacceptor*)arg;
	acceptor_receive_trim(a->state, trim);
}

static void
send_acceptor_state(unsigned long arg)
{
	printk(KERN_INFO "Called");
	struct evacceptor* a = (struct evacceptor*)arg;
	paxos_message msg = {.type = PAXOS_ACCEPTOR_STATE};
	acceptor_set_current_state(a->state, &msg.u.state);
	peers_foreach_client(a->peers, peer_send_paxos_message, &msg);
	// mod_timer(&a->timer_ev, jiffies + timeval_to_jiffies(&(a->timer_tv)));
}

static void check_timeout(unsigned long data){
	unsigned long * arr = (unsigned long *) data;
	struct evacceptor* a = (struct evacceptor *) arr[0];
	struct udp_service * k = (struct udp_service *) arr[1];
	printk(KERN_INFO "acceptor timeout\n");
	// printk(KERN_INFO "k null? %s k->called[ACC_TIM] null? %s\n", k == NULL? "Yes" : "no", &k->called[ACC_TIM] == NULL ? "Yes" : "no");

	if(atomic_read(&(k->called[ACC_TIM])) == 0){
		printk(KERN_INFO "acceptor timeout set callback to 1\n");
		atomic_set(&k->called[ACC_TIM],1);
	}
	mod_timer(&a->timer_ev, jiffies + timeval_to_jiffies(&(a->timer_tv)));
	printk(KERN_INFO "acceptor Restarted timer");
}


struct evacceptor*
evacceptor_init_internal(int id, struct evpaxos_config* c, struct peers* p, udp_service * k)
{
	struct evacceptor* acceptor;

	acceptor = kmalloc(sizeof(struct evacceptor), GFP_KERNEL);
	acceptor->state = acceptor_new(id);
	acceptor->peers = p;

	peers_subscribe(p, PAXOS_PREPARE, evacceptor_handle_prepare, acceptor);
	peers_subscribe(p, PAXOS_ACCEPT, evacceptor_handle_accept, acceptor);
	peers_subscribe(p, PAXOS_REPEAT, evacceptor_handle_repeat, acceptor);
	peers_subscribe(p, PAXOS_TRIM, evacceptor_handle_trim, acceptor);
	printk(KERN_INFO "Acceptor: Subscribed to PAXOS_PREPARE, PAXOS_ACCEPT, PAXOS_TRIM, PAXOS_REPEAT");

	acceptor->arr[0] = (unsigned long) acceptor;
	acceptor->arr[1] = (unsigned long) k;
	atomic_set(&k->called[ACC_TIM], 0);
	k->timer_cb[ACC_TIM] = send_acceptor_state;
	k->data[ACC_TIM] = (unsigned long) acceptor;
	setup_timer( &acceptor->timer_ev,  check_timeout, (unsigned long) acceptor->arr);
	acceptor->timer_tv = (struct timeval){1, 0};
	mod_timer(&acceptor->timer_ev, jiffies + timeval_to_jiffies(&acceptor->timer_tv));

	return acceptor;
}

struct evacceptor*
evacceptor_init(int id, const char* config_file, udp_service * k)
{
	struct evpaxos_config* config = evpaxos_config_read(config_file);
	if (config  == NULL)
		return NULL;

	int acceptor_count = evpaxos_acceptor_count(config);
	if (id < 0 || id >= acceptor_count) {
		paxos_log_error("Invalid acceptor id: %d.", id);
		paxos_log_error("Should be between 0 and %d", acceptor_count);
		evpaxos_config_free(config);
		return NULL;
	}
	struct sockaddr_in send_add = evpaxos_acceptor_address(config,id);
	struct sockaddr_in rcv_add;
	memcpy(&send_add, &rcv_add, sizeof(struct sockaddr_in));
	send_add.sin_port = 0; // random port for sending
	struct peers* peers = peers_new(&send_add, &rcv_add, config, id);
	if(peers_sock_init(peers,k) >= 0){
		struct evacceptor* acceptor = evacceptor_init_internal(id, config, peers, k);
		evpaxos_config_free(config);
		return acceptor;
	}
	evpaxos_config_free(config);
	return NULL;
}

void stop_acceptor_timer(struct evacceptor * a){
	printk("Acceptor Timer stopped");
	del_timer(&a->timer_ev);
}

void
evacceptor_free_internal(struct evacceptor* a)
{
	del_timer(&a->timer_ev);
	acceptor_free(a->state);
	kfree(a);
}

void
evacceptor_free(struct evacceptor* a)
{
	peers_free(a->peers);
	evacceptor_free_internal(a);
}
