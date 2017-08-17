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


#include "peers.h"
#include "message.h"
#include <linux/errno.h>

#include <linux/types.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include "kernel_udp.h"

#define HANDLE_BIG_PKG 0

struct peer
{
	int id;
	struct sockaddr_in addr;
	struct peers* peers;
};

struct subscription
{
	paxos_message_type type;
	peer_cb callback;
	void* arg;
};

struct peers
{
	int peers_count, clients_count;
	struct peer** peers;   /* peers we connected to */
	struct peer** clients; /* peers we accepted connections from */
	struct socket * sock_send;
	// struct socket * sock_rcv;
	struct peer * me_send;
	// struct peer * me_rcv;
	struct evpaxos_config* config;
	int subs_count;
	struct subscription subs[32];
};

static struct peer* make_peer(struct peers* p, int id, struct sockaddr_in* in);
static void free_peer(struct peer* p);
static void free_all_peers(struct peer** p, int count);
static int on_read(char * data,struct peer * arg, int size);

struct peers* peers_new(struct sockaddr_in * send_addr, struct evpaxos_config* config, int id)
{
	struct peers* p = kmalloc(sizeof(struct peers), GFP_KERNEL);
	p->peers_count = 0;
	p->clients_count = 0;
	p->subs_count = 0;
	p->peers = NULL;
	p->clients = NULL;
	p->me_send = make_peer(p, id, send_addr);
	// p->me_rcv = make_peer(p, id, rcv_addr);
	p->config = config;
	return p;
}

void
peers_free(struct peers* p)
{
	free_all_peers(p->peers, p->peers_count);
	free_all_peers(p->clients, p->clients_count);
	kfree(p->me_send);
	// kfree(p->me_rcv);
	kfree(p);
}

int
peers_count(struct peers* p)
{
	return p->peers_count;
}

void
peers_foreach_acceptor(struct peers* p, peer_iter_cb cb, void* arg)
{
	int i;
	for (i = 0; i < p->peers_count; ++i){
		cb(p->peers[i], arg);
	}
}

void
peers_foreach_client(struct peers* p, peer_iter_cb cb, void* arg)
{
	int i;
	for (i = 0; i < p->clients_count; ++i)
		cb(p->clients[i], arg);
}

struct peer*
peers_get_acceptor(struct peers* p, int id)
{
	int i;
	for (i = 0; p->peers_count; ++i)
		if (p->peers[i]->id == id)
			return p->peers[i];
	return NULL;
}

// client, add to peers
void add_proposers_from_config(int myid, struct peers * p){
	struct sockaddr_in addr;
	int n = evpaxos_proposer_count(p->config);
	p->peers = krealloc(p->peers, sizeof(struct peer*) * n, GFP_KERNEL);
	for(int i = 0; i < n; i++){
		if(i != myid){
			addr = evpaxos_proposer_address(p->config, i);
			p->peers[p->peers_count] = make_peer(p, p->peers_count, &addr);
			p->peers_count++;
		}
	}
}

// learner and proposer, add to peers
void add_acceptors_from_config(int myid, struct peers * p){
	struct sockaddr_in addr;
	int n = evpaxos_acceptor_count(p->config);
	p->peers = krealloc(p->peers, sizeof(struct peer*) * n, GFP_KERNEL);
	for(int i = 0; i < n; i++){
		if(i != myid){
			addr = evpaxos_acceptor_address(p->config, i);
			p->peers[p->peers_count] = make_peer(p, p->peers_count, &addr);
			p->peers_count++;
		}
	}
}

void printall(struct peers * p){
	printk(KERN_INFO "PEERS we connect to");
	for(int i = 0; i < p->peers_count; i++){
		printk(KERN_INFO "id = %d, ip = %pI4, port = %d", p->peers[i]->id, &(p->peers[i]->addr.sin_addr), ntohs(p->peers[i]->addr.sin_port) );
	}

	printk(KERN_INFO "CLIENTS we receive connections \n(will be updated as message are received)");
	for(int i = 0; i < p->clients_count; i++){
		printk(KERN_INFO "id = %d, ip = %pI4, port = %d", p->clients[i]->id, &(p->clients[i]->addr.sin_addr), ntohs(p->clients[i]->addr.sin_port) );
	}
}

struct socket * get_send_socket(struct peer * p){
	return p->peers->sock_send;
}

// struct socket * get_rcv_socket(struct peer * p){
// 	return p->peers->sock_rcv;
// }

struct sockaddr_in * get_sockaddr(struct peer * p){
	return &p->addr;
}

struct peer * get_me_send(struct peers * p){
	return p->me_send;
}

// struct peer * get_me_rcv(struct peers * p){
// 	return p->me_rcv;
// }

int
peer_get_id(struct peer* p)
{
	return p->id;
}

static void add_or_update_client(struct sockaddr_in * addr, struct peers * p){
	for (int i = 0; i < p->clients_count; ++i){
		if(memcmp(&(addr->sin_port), &(p->clients[i]->addr.sin_port), sizeof(unsigned short)) == 0
		&& memcmp(&(addr->sin_addr), &(p->clients[i]->addr.sin_addr), sizeof(struct in_addr)) == 0){
			return;
		}
	}
	p->clients = krealloc(p->clients, sizeof(struct peer) * p->clients_count + 1, GFP_KERNEL);
	p->clients[p->clients_count] = make_peer(p, p->clients_count, addr);
	p->clients_count++;
}

int peers_sock_init(struct peers* p, udp_service * k){
	// int i = udp_server_init(k, &p->sock_rcv, &p->me_rcv->addr, &k->rcv_socket_allocated);
	return udp_server_init(k, &p->sock_send, &p->me_send->addr, &k->send_socket_allocated);
	// if(i == 0 && j == 0){
	// 	return 0;
	// }
	// return -1;
}

int
peers_listen(struct peers* p, udp_service * k)
{
	int ret, first_time = 0;
	struct sockaddr_in address;
	unsigned char * in_buf = kmalloc(MAX_UDP_SIZE, GFP_KERNEL);

	#if HANDLE_BIG_PKG
		unsigned char * bigger_buff = NULL;
		int n_packet_toget =0, size_bigger_buf = 0;
	#endif
	printk(KERN_INFO "%s Listening", k->name);
	while(1){

		if(kthread_should_stop() || signal_pending(current)){
			printk(KERN_INFO "Stopped!");
			// check_sock_allocation(k, p->sock_rcv, &k->rcv_socket_allocated);
      check_sock_allocation(k, p->sock_send, &k->send_socket_allocated);
      kfree(in_buf);

			#if HANDLE_BIG_PKG
				if(bigger_buff != NULL){
					kfree(bigger_buff);
				}
			#endif
      return 0;
    }

		memset(in_buf, '\0', MAX_UDP_SIZE);
    memset(&address, 0, sizeof(struct sockaddr_in));
		// printk(KERN_INFO "Receiving...");
		ret = udp_server_receive(p->sock_send, &address, in_buf, MSG_WAITALL, k);
		struct peer tmp;
		memcpy(&tmp.addr, &address, sizeof(struct sockaddr_in));
		tmp.peers = p;

		// printk(KERN_INFO "End receiving");
		if(ret > 0){
			if(first_time == 0){
				add_or_update_client(&address, p);
				ret = on_read(in_buf, &tmp, MAX_UDP_SIZE);
				#if HANDLE_BIG_PKG
				// 	if(ret != 0){
				// 		while(ret > 0){
				// 			ret -= MAX_UDP_SIZE;
				// 			n_packet_toget++;
				// 		}
				// 		size_bigger_buf = MAX_UDP_SIZE * (n_packet_toget +1);
				// 		bigger_buff = krealloc(in_buf, size_bigger_buf, GFP_KERNEL);
				// 		in_buf+=MAX_UDP_SIZE;
				// 		memset(in_buf, '\0', MAX_UDP_SIZE * n_packet_toget);
				// 		first_time = 1;
				// 	}
				// }else{
				// 	strncat(bigger_buff, in_buf, MAX_UDP_SIZE);
				// 	n_packet_toget--;
				// 	if(n_packet_toget == 0){
					// 	first_time = 0;
					// 	in_buf = bigger_buff;
					// 	on_read(bigger_buff, p->me, size_bigger_buf);
					// }
				#endif
			}
		}
		// printk(KERN_INFO "Listening done, checking for timer callback");
		for(int i = 0; i < N_TIMER; i++){
			// k->timer_cb[i] != NULL is a safety check
			if(k->timer_cb[i] != NULL && atomic_read(&k->called[i]) == 1){
				k->timer_cb[i](k->data[i]);
				atomic_set(&k->called[i], 0);
			}

			// if(atomic_read(&k->sending[i]) == 0){
			// 	// printk(KERN_INFO "Sending the queue");
			// 	atomic_set(&k->sending[i], 1);
			// 	struct command * tmp = k->to_send[i];
			// 	struct command * del;
			// 	while(tmp != NULL){
			// 		tmp->send_function(tmp->s, tmp->addr, tmp->arg);
			// 		del = tmp;
			// 		tmp = tmp->next;
			// 		k->to_send[i] = tmp;
			// 		kfree(del);
			// 	}
			// 	k->last_send[i] = NULL;
			// 	// printk(KERN_INFO "Done sending the queue");
			// 	atomic_set(&k->sending[i], 0);
			// }
		}
	}
	return 1;
}

void
peers_subscribe(struct peers* p, paxos_message_type type, peer_cb cb, void* arg)
{
	struct subscription* sub = &p->subs[p->subs_count];
	sub->type = type;
	sub->callback = cb;
	sub->arg = arg;
	p->subs_count++;
}

static void
dispatch_message(struct peer* p, paxos_message* msg)
{
	int i;
	for (i = 0; i < p->peers->subs_count; ++i) {
		struct subscription* sub = &p->peers->subs[i];
		if (sub->type == msg->type){
			sub->callback(p, msg, sub->arg);
			break;
		}
	}
	// if(msg->type == PAXOS_LEARNER_HI){
	// 	 printk(KERN_INFO "Received Hi");
	// }
}

static int
on_read(char * data,struct peer * arg, int size)
{
	paxos_message msg;
	int ret;
	if((ret = recv_paxos_message(data, &msg, size)) != 0){// returns if the packet is partial
		return ret;
	}
	dispatch_message(arg, &msg);
	paxos_message_destroy(&msg);
	return 0;

}

static struct peer*
make_peer(struct peers* peers, int id, struct sockaddr_in* addr)
{
	struct peer* p = kmalloc(sizeof(struct peer), GFP_KERNEL);
	p->id = id;
	if(addr != NULL)
		p->addr = *addr;
	p->peers = peers;
	return p;
}

static void
free_all_peers(struct peer** p, int count)
{
	int i;
	for (i = 0; i < count; i++)
		free_peer(p[i]);
	if (count > 0)
		kfree(p);
}

static void
free_peer(struct peer* p)
{
	kfree(p);
}
