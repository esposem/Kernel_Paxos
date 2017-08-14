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
#include "learner.h"
#include "peers.h"
#include "message.h"
#include <linux/timer.h>
#include <linux/slab.h>

struct evlearner
{
	struct learner* state;      /* The actual learner */
	deliver_function delfun;    /* Delivery callback */
	void* delarg;               /* The argument to the delivery callback */
	struct timer_list hole_timer;   /* Timer to check for holes */
	struct timeval tv;          /* Check for holes every tv units of time */
	struct peers* acceptors;    /* Connections to acceptors */
	unsigned long arr[2];
};


struct socket * get_sock(struct evlearner * l){
	struct peer * p =  get_me_send(l->acceptors);
	return get_send_socket(p);
}

struct sockaddr_in * get_sockad(struct evlearner * l){
	struct peer * p =  get_me_send(l->acceptors);
	return get_sockaddr(p);
}


static void
peer_send_repeat(struct peer* p, void* arg)
{
	send_paxos_repeat(get_send_socket(p), get_sockaddr(p), arg);
}

static void
peer_send_hi(struct peer* p, void* arg)
{
	send_paxos_learner_hi(get_send_socket(p), get_sockaddr(p), NULL);
}


static void
evlearner_check_holes(unsigned long arg)
{
	printk(KERN_INFO "Checking holes");
	paxos_repeat msg;
	int chunks = 10;
	struct evlearner* l = (struct evlearner *) arg;
	if (learner_has_holes(l->state, &msg.from, &msg.to)) {
		if ((msg.to - msg.from) > chunks)
			msg.to = msg.from + chunks;
		peers_foreach_acceptor(l->acceptors, peer_send_repeat, &msg);
		paxos_log_debug("Learner: sent PAXOS_REPEAT to all acceptors, missing %d chunks", chunks);
	}
	// mod_timer(&l->hole_timer, jiffies + timeval_to_jiffies(&l->tv));

}

static void
evlearner_deliver_next_closed(struct evlearner* l)
{
	paxos_accepted deliver;
	while (learner_deliver_next(l->state, &deliver)) {
		l->delfun(
			deliver.iid,
			deliver.value.paxos_value_val,
			deliver.value.paxos_value_len,
			l->delarg);
		paxos_accepted_destroy(&deliver);
	}
}

static void check_timeout(unsigned long data){
	// printk(KERN_INFO "learner timeout\n");
	unsigned long * arr = (unsigned long *) data;
	struct evlearner* l = (struct evlearner *) arr[0];
	struct udp_service * k = (struct udp_service *) arr[1];
	if(atomic_read(&k->called[LEA_TIM]) == 0){
		// printk(KERN_INFO "learner timeout set callback to 1\n");
		atomic_set(&k->called[LEA_TIM],1);
	}
	mod_timer(&l->hole_timer, jiffies + timeval_to_jiffies(&l->tv));
	// printk(KERN_INFO "learner Restarted timer");

}


/*
	Called when an accept_ack is received, the learner will update it's status
    for that instance and afterwards check if the instance is closed
*/
static void
evlearner_handle_accepted(struct peer* p, paxos_message* msg, void* arg)
{
	struct evlearner* l = arg;
	paxos_log_debug("Received PAXOS_ACCEPTED, update status and check instance is closed");
	learner_receive_accepted(l->state, &msg->u.accepted);
	evlearner_deliver_next_closed(l);
}

struct evlearner*
evlearner_init_internal(struct evpaxos_config* config, struct peers* peers,
	deliver_function f, void* arg, udp_service * k)
{
	int acceptor_count = evpaxos_acceptor_count(config);
	struct evlearner* learner = kmalloc(sizeof(struct evlearner), GFP_KERNEL);

	learner->delfun = f;
	learner->delarg = arg;
	learner->state = learner_new(acceptor_count);
	printk(KERN_INFO "Learner: allocated a new learner");
	learner->acceptors = peers;


	peers_subscribe(peers, PAXOS_ACCEPTED, evlearner_handle_accepted, learner);
	printk(KERN_INFO "Learner: Subscribed to PAXOS_ACCEPTED");

	peers_foreach_acceptor(peers, peer_send_hi, NULL);
	printk(KERN_INFO "Learner: Sent hi to acceptors");

	// setup hole checking timer
	learner->arr[0] = (unsigned long) learner;
	learner->arr[1] = (unsigned long) k;
	atomic_set(&k->called[LEA_TIM], 0);
	k->timer_cb[LEA_TIM] = evlearner_check_holes;
	k->data[LEA_TIM] = (unsigned long) learner;
	learner->tv.tv_sec = 0;
	learner->tv.tv_usec = 100000;
	setup_timer(&learner->hole_timer, check_timeout, (unsigned long) learner->arr);
	mod_timer(&learner->hole_timer, jiffies + timeval_to_jiffies(&learner->tv));
	// printk(KERN_INFO "Learner: added timer to check if after x there is a hole in the instances. If so, send PAXOS_REPEAT to acceptor");
	return learner;
}

struct evlearner*
evlearner_init(const char* config_file, deliver_function f, void* arg,
	udp_service * k)
{
	struct evpaxos_config* c = evpaxos_config_read(config_file);
	if (c == NULL){
		return NULL;
	}else{
		paxos_log_debug("Learner: read config file");
	}

	struct sockaddr_in addr;
	addr.sin_port = 0;
	addr.sin_addr.s_addr = INADDR_ANY;
	struct peers* peers = peers_new(&addr, &addr, c, -1);
	add_acceptors_from_config(-1, peers);
	paxos_log_debug("Learner: Connected to acceptors");
	if(peers_sock_init(peers, k) >= 0){
		struct evlearner* l = evlearner_init_internal(c, peers, f, arg, k);
		evpaxos_config_free(c);
		return l;
	}
	evpaxos_config_free(c);
	return NULL;
}

void paxos_learner_listen(udp_service * k, struct evlearner * ev){
	peers_listen(ev->acceptors, k);
}

void
evlearner_free_internal(struct evlearner* l)
{
	del_timer(&l->hole_timer);
	learner_free(l->state);
	kfree(l);
}

void stop_learner_timer(struct evlearner * l){
	printk("Learner Timer stopped");
	del_timer(&l->hole_timer);
}

void
evlearner_free(struct evlearner* l)
{
	peers_free(l->acceptors);
	evlearner_free_internal(l);
}

void
evlearner_set_instance_id(struct evlearner* l, unsigned iid)
{
	learner_set_instance_id(l->state, iid);
}

static void
peer_send_trim(struct peer* p, void* arg)
{
	send_paxos_trim(get_send_socket(p), get_sockaddr(p), arg);
}

void
evlearner_send_trim(struct evlearner* l, unsigned iid)
{
	paxos_trim trim = {iid};
	paxos_log_debug("Sent PAXOS_TRIM to all acceptors");
	peers_foreach_acceptor(l->acceptors, peer_send_trim, &trim);
}
