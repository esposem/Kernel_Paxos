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
};


static void
peer_send_prepare(struct peer* p, void* arg)
{
	send_paxos_prepare(get_socket(p), get_sockaddr(p), arg);
}

static void
peer_send_accept(struct peer* p, void* arg)
{
	send_paxos_accept(get_socket(p), get_sockaddr(p), arg);
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
	paxos_log_debug("Proposer: Opened %d new instances", count);
}

static void
try_accept(struct evproposer* p)
{
	paxos_accept accept;
	paxos_log_debug("Sending propose to all acceptors");
	while (proposer_accept(p->state, &accept))
		peers_foreach_acceptor(p->peers, peer_send_accept, &accept);
	proposer_preexecute(p);
}

static void
evproposer_handle_promise(struct peer* p, paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	paxos_prepare prepare;
	paxos_promise* pro = &msg->u.promise;
	int preempted = proposer_receive_promise(proposer->state, pro, &prepare);
	if (preempted){
		paxos_log_debug("Proposer: received PREEMPTED\nSending prepare to all acceptors and preexecuting");
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
		paxos_log_debug("Proposer: received ACCEPT REQUEST\nSending accept to all acceptors");
		try_accept(proposer);
	}
}

static void
evproposer_handle_preempted(struct peer* p, paxos_message* msg, void* arg)
{
	struct evproposer* proposer = arg;
	paxos_prepare prepare;
	int preempted = proposer_receive_preempted(proposer->state,
		&msg->u.preempted, &prepare);
	if (preempted) {
		paxos_log_debug("Proposer: received PREEMPTED\nSending prepare to all acceptors and preexecuting");
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

	paxos_log_debug("Proposer: received a CLIENT VALUE, creating a new propose and preexecuting");
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
evproposer_check_timeouts(unsigned long arg)
{
	struct evproposer* p = (struct evproposer *) arg;
	struct timeout_iterator* iter = proposer_timeout_iterator(p->state);

	paxos_prepare pr;
	while (timeout_iterator_prepare(iter, &pr)) {
		paxos_log_info("Instance %d timed out in phase 1.", pr.iid);
		peers_foreach_acceptor(p->peers, peer_send_prepare, &pr);
	}

	paxos_accept ar;
	while (timeout_iterator_accept(iter, &ar)) {
		paxos_log_info("Instance %d timed out in phase 2.", ar.iid);
		peers_foreach_acceptor(p->peers, peer_send_accept, &ar);
	}

	timeout_iterator_free(iter);
	mod_timer(&p->timeout_ev, jiffies + timeval_to_jiffies(&(p->tv)));
}

static void
evproposer_preexec_once(struct evproposer * arg)
{
	struct evproposer* p = arg;
	proposer_preexecute(p);
}

struct evproposer*
evproposer_init_internal(int id, struct evpaxos_config* c, struct peers* peers)
{
	struct evproposer* p;
	int acceptor_count = evpaxos_acceptor_count(c);

	p = kmalloc(sizeof(struct evproposer), GFP_KERNEL);
	p->id = id;
	p->preexec_window = paxos_config.proposer_preexec_window;

	peers_subscribe(peers, PAXOS_PROMISE, evproposer_handle_promise, p);
	peers_subscribe(peers, PAXOS_ACCEPTED, evproposer_handle_accepted, p);
	peers_subscribe(peers, PAXOS_PREEMPTED, evproposer_handle_preempted, p);
	peers_subscribe(peers, PAXOS_CLIENT_VALUE, evproposer_handle_client_value, p);
	peers_subscribe(peers, PAXOS_ACCEPTOR_STATE,
		evproposer_handle_acceptor_state, p);
	paxos_log_debug("Proposer: Subscribed to PAXOS_PROMISE, PAXOS_ACCEPTED, PAXOS_PREEMPTED, PAXOS_CLIENT_VALUE");

	// Setup timeout
	p->tv.tv_sec = paxos_config.proposer_timeout;
	p->tv.tv_usec = 0;
	setup_timer( &p->timeout_ev,  evproposer_check_timeouts, (unsigned long) p);
	mod_timer(&p->timeout_ev, jiffies + timeval_to_jiffies(&p->tv));

	// printk(KERN_INFO "number of acceptor : %d", acceptor_count);
	p->state = proposer_new(p->id, acceptor_count);
	paxos_log_debug("Proposer: Created an internal proposer");
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
		paxos_log_error("Invalid proposer id: %d", id);
		return NULL;
	}

	struct sockaddr_in addr = evpaxos_proposer_address(config,id);
	struct peers* peers = peers_new(&addr, config, id);
	add_acceptors_from_config(-1, peers);

	if(peers_sock_init(peers, k) == 0){
		struct evproposer* p = evproposer_init_internal(id, config, peers);
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
