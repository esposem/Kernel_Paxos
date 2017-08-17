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
#include "peers.h"
#include "message.h"
#include "proposer.h"
#include <linux/timer.h>
#include <linux/slab.h>

struct evproposer
{
	int id;
	int preexec_window;
	struct proposer* state;
	struct peers* peers;
	struct timeval tv;
	struct timer_list timeout_ev;
	unsigned long arr[2];
	struct udp_service * k;
	atomic_t iterator_empty; // 1 yes 0 no
};

struct evproposer * props = NULL;

static void
check_add_queue(void (*send_function)(struct socket *, struct sockaddr_in *, void *), void * arg, struct peer * p){
	// if(atomic_read(&props->k->sending[PROP_TIM]) == 1){
	// 	// printk(KERN_INFO "Adding send to the queue");
	// 	struct command * tmp;
	// 	tmp = kmalloc(sizeof(struct command), GFP_KERNEL);
	// 	tmp->send_function = send_function;
	// 	tmp->s = get_send_socket(p);
	// 	tmp->addr = get_sockaddr(p);
	// 	tmp->arg = arg;
	// 	if(props->k->to_send[PROP_TIM] == NULL){
	// 		props->k->to_send[PROP_TIM] = tmp;
	// 		props->k->last_send[PROP_TIM] = tmp;
	// 	}else{
	// 		props->k->last_send[PROP_TIM]->next = tmp;
	// 		props->k->last_send[PROP_TIM] = tmp;
	// 	}
	// }else{
	// 	atomic_set(&props->k->sending[PROP_TIM], 1);
		send_function(get_send_socket(p), get_sockaddr(p), arg);
	// 	atomic_set(&props->k->sending[PROP_TIM], 0);
	// }
}


static void
peer_send_prepare(struct peer* p, void* arg)
{
	check_add_queue(send_paxos_prepare, arg, p);
}

static void
peer_send_accept(struct peer* p, void* arg)
{
	check_add_queue(send_paxos_accept, arg, p);
}

static void
proposer_preexecute(struct evproposer* p)
{
	int i;
	paxos_prepare pr;
	int count = p->preexec_window - proposer_prepared_count(p->state);
	if (count <= 0) return;
	for (i = 0; i < count; i++) {
		proposer_prepare(p->state, &pr);
		peers_foreach_acceptor(p->peers, peer_send_prepare, &pr);
	}
	printk(KERN_INFO "Proposer: Opened %d new instances", count);
}

static void
try_accept(struct evproposer* p)
{
	paxos_accept accept;
	int i = 0;
	while (proposer_accept(p->state, &accept)){
		i = 1;
		peers_foreach_acceptor(p->peers, peer_send_accept, &accept);
	}
	if(i == 1){
		printk(KERN_INFO "Proposer: Sending accept to all acceptors");
	}
	proposer_preexecute(p);
}

static void
evproposer_handle_promise(struct peer* p, paxos_message* msg, void* arg)
{
	printk(KERN_INFO "Proposer: received PROMISE");
	struct evproposer* proposer = arg;
	paxos_prepare prepare;
	paxos_promise* pro = &msg->u.promise;
	int preempted = proposer_receive_promise(proposer->state, pro, &prepare);
	if (preempted){
		// printk(KERN_INFO "Proposer: received PREEMPTED\nSending prepare to all acceptors and preexecuting");
		peers_foreach_acceptor(proposer->peers, peer_send_prepare, &prepare);
	}
	try_accept(proposer);
}

static void
evproposer_handle_accepted(struct peer* p, paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	paxos_accepted* acc = &msg->u.accepted;
	if (proposer_receive_accepted(proposer->state, acc)){
		printk(KERN_INFO "Proposer: received ACCEPT REQUEST\nSending accept to all acceptors");
		try_accept(proposer);
	}
}

static void
evproposer_handle_preempted(struct peer* p, paxos_message* msg, void* arg)
{
	printk(KERN_INFO "Proposer: received PREEMPTED");
	struct evproposer* proposer = arg;
	paxos_prepare prepare;
	int preempted = proposer_receive_preempted(proposer->state,
		&msg->u.preempted, &prepare);
	if (preempted) {
		// printk(KERN_INFO "Proposer: received PREEMPTED\nSending prepare to all acceptors and preexecuting");
		peers_foreach_acceptor(proposer->peers, peer_send_prepare, &prepare);
		try_accept(proposer);
	}
}

static void
evproposer_handle_client_value(struct peer* p, paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	struct paxos_client_value* v = &msg->u.client_value;

	proposer_propose(proposer->state,
		v->value.paxos_value_val,
		v->value.paxos_value_len);

	printk(KERN_INFO "Proposer: received a CLIENT VALUE, creating a new propose and preexecuting");
	try_accept(proposer);
}

static void
evproposer_handle_acceptor_state(struct peer* p, paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	struct paxos_acceptor_state* acc_state = &msg->u.state;
	proposer_receive_acceptor_state(proposer->state, acc_state);
}

static void
evproposer_preexec_once(struct evproposer * arg)
{
	struct evproposer* p = arg;
	proposer_preexecute(p);
}

static void
evproposer_check_timeouts(unsigned long arg)
{
	// printk(KERN_INFO "Proposer: evproposer_check_timeouts");

	struct evproposer* p = (struct evproposer *) arg;
	struct timeout_iterator* iter = proposer_timeout_iterator(p->state);
	printk(KERN_INFO "Proposer: Instances timed out in phase 1.");

	paxos_prepare pr;
	while (timeout_iterator_prepare(iter, &pr)) {
		// printk(KERN_INFO "Proposer: Instance %d timed out in phase 1.", pr.iid);
		peers_foreach_acceptor(p->peers, peer_send_prepare, &pr);
	}

	paxos_accept ar;
	while (timeout_iterator_accept(iter, &ar)) {
		// printk(KERN_INFO "Proposer: Instance %d timed out in phase 2.", ar.iid);
		peers_foreach_acceptor(p->peers, peer_send_accept, &ar);
	}

	timeout_iterator_free(iter);
	mod_timer(&p->timeout_ev, jiffies + timeval_to_jiffies(&p->tv));
}

static void check_timeout(unsigned long data){
	// printk(KERN_INFO "Proposer: Timer called");
	unsigned long * arr = (unsigned long *) data;
	struct evproposer* p = (struct evproposer *) arr[0];
	struct udp_service * k = (struct udp_service *) arr[1];

	if(atomic_read(&k->called[PROP_TIM]) == 0){
		// printk(KERN_INFO "proposer timeout set callback to 1\n");
		atomic_set(&k->called[PROP_TIM],1);
	}

	// printk(KERN_INFO "Proposer: Restarted timer");
	// mod_timer(&p->timeout_ev, jiffies + timeval_to_jiffies(&p->tv));
}


struct evproposer*
evproposer_init_internal(int id, struct evpaxos_config* c, struct peers* peers, udp_service * k)
{
	struct evproposer* p;
	int acceptor_count = evpaxos_acceptor_count(c);

	p = kmalloc(sizeof(struct evproposer), GFP_KERNEL);
	p->k = k;
	props = p;
	p->id = id;
	atomic_set(&p->iterator_empty, 1);
	p->preexec_window = paxos_config.proposer_preexec_window;

	peers_subscribe(peers, PAXOS_PROMISE, evproposer_handle_promise, p);
	peers_subscribe(peers, PAXOS_ACCEPTED, evproposer_handle_accepted, p);
	peers_subscribe(peers, PAXOS_PREEMPTED, evproposer_handle_preempted, p);
	peers_subscribe(peers, PAXOS_CLIENT_VALUE, evproposer_handle_client_value, p);
	peers_subscribe(peers, PAXOS_ACCEPTOR_STATE,
		evproposer_handle_acceptor_state, p);
	printk(KERN_INFO "Proposer: Subscribed to PAXOS_PROMISE, PAXOS_ACCEPTED, PAXOS_PREEMPTED, PAXOS_CLIENT_VALUE");

	// Setup timeout
	p->arr[0] = (unsigned long) p;
	p->arr[1] = (unsigned long) k;
	atomic_set(&k->called[PROP_TIM], 0);
	k->timer_cb[PROP_TIM] = evproposer_check_timeouts;
	k->data[PROP_TIM] = (unsigned long) p;
	p->tv.tv_sec = paxos_config.proposer_timeout;
	p->tv.tv_usec = 0;
	setup_timer( &p->timeout_ev,  check_timeout, (unsigned long) p->arr);
	mod_timer(&p->timeout_ev, jiffies + timeval_to_jiffies(&p->tv));

	p->state = proposer_new(p->id, acceptor_count);
	printk(KERN_INFO "Proposer: Created an internal proposer");
	p->peers = peers;

	evproposer_preexec_once(p);

	return p;
}

struct evproposer*
evproposer_init(int id, const char* config_file, udp_service * k)
{
	struct evpaxos_config* config = evpaxos_config_read(config_file);

	if (config == NULL)
		return NULL;

	// Check id validity of proposer_id
	if (id < 0 || id >= MAX_N_OF_PROPOSERS) {
		printk(KERN_INFO "Invalid proposer id: %d", id);
		return NULL;
	}

	struct sockaddr_in send_addr = evpaxos_proposer_address(config,id);
	// struct sockaddr_in send_addr;
	// memcpy(&send_addr, &rcv_addr, sizeof(struct sockaddr_in));
	// send_addr.sin_port = 0;
	struct peers* peers = peers_new(&send_addr, config, id);
	add_acceptors_from_config(-1, peers);
	printall(peers);
	if(peers_sock_init(peers, k) == 0){
		struct evproposer* p = evproposer_init_internal(id, config, peers, k);
		evpaxos_config_free(config);
		return p;
	}
	evpaxos_config_free(config);
	return NULL;
}

void paxos_proposer_listen(udp_service * k, struct evproposer * ev){
	peers_listen(ev->peers, k);
}

void stop_proposer_timer(struct evproposer * p){
	printk("Proposer Timer stopped");
	del_timer(&p->timeout_ev);
	printall(p->peers);
}

void
evproposer_free_internal(struct evproposer* p)
{
	del_timer(&p->timeout_ev);
	proposer_free(p->state);
	kfree(p);
}

void
evproposer_free(struct evproposer* p)
{
	peers_free(p->peers);
	evproposer_free_internal(p);
}

void
evproposer_set_instance_id(struct evproposer* p, unsigned iid)
{
	proposer_set_instance_id(p->state, iid);
}
