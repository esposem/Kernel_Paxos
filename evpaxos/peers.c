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

#include "eth.h"

#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/types.h>

#define HANDLE_BIG_PKG 0

struct peer {
  int id;
  eth_address addr[ETH_ALEN];
  struct peers *peers;
};

struct subscription {
  paxos_message_type type;
  peer_cb callback;
  void *arg;
};

struct peers {
  int peers_count, clients_count;
  struct peer **peers;   /* peers we connected to */
  struct peer **clients; /* peers we accepted connections from */
  struct peer *me_send;
  struct evpaxos_config *config;
  struct net_device *dev;
};

int *peers_received_ok = NULL;
int ok_received;

static struct peer *make_peer(struct peers *p, int id, eth_address *in);
static void free_peer(struct peer *p);
static void free_all_peers(struct peer **p, int count);

struct peers *peers_new(struct evpaxos_config *config, int id, char *if_name) {
  struct peers *p = pmalloc(sizeof(struct peers));
  p->peers_count = 0;
  p->clients_count = 0;
  p->peers = NULL;
  p->clients = NULL;

  p->config = config;
  p->dev = eth_init(if_name);
  if (p->dev == NULL) {
    printk(KERN_ERR "Interface not found: %s.", if_name);
    return NULL;
  }
  p->me_send = make_peer(p, id, p->dev->dev_addr);
  return p;
}

void peers_free(struct peers *p) {
  eth_destroy(p->dev);
  free_all_peers(p->peers, p->peers_count);
  free_all_peers(p->clients, p->clients_count);
  kfree(p->me_send);
  kfree(p);
}

int peers_count(struct peers *p) { return p->peers_count; }

eth_address *get_addr(struct peer *p) { return p->addr; }

void peers_foreach_acceptor(struct peers *p, peer_iter_cb cb, void *arg) {
  int i;
  for (i = 0; i < p->peers_count; ++i) {
    cb(p->dev, p->peers[i], arg);
  }
}

void peers_foreach_client(struct peers *p, peer_iter_cb cb, void *arg) {
  int i;
  for (i = 0; i < p->clients_count; ++i)
    cb(p->dev, p->clients[i], arg);
}

struct peer *peers_get_acceptor(struct peers *p, int id) {
  int i;
  for (i = 0; p->peers_count; ++i)
    if (p->peers[i]->id == id)
      return p->peers[i];
  return NULL;
}

// add all acceptors in the peers
void add_acceptors_from_config(int myid, struct peers *p) {
  eth_address *addr;
  int n = evpaxos_acceptor_count(p->config);
  p->peers = prealloc(p->peers, sizeof(struct peer *) * n);
  int i;
  for (i = 0; i < n; i++) {
    if (i != myid) {
      addr = evpaxos_acceptor_address(p->config, i);
      p->peers[p->peers_count] = make_peer(p, i, addr);
      p->peers_count++;
    }
  }
  paxos_log_debug("ALLOCATED %d", p->peers_count);
  peers_received_ok = pmalloc(sizeof(int) * p->peers_count);
  memset(peers_received_ok, 0, sizeof(int) * p->peers_count);
}

void printall(struct peers *p, char *name) {
  printk(KERN_INFO "\t%s\n", name);
  printk(KERN_INFO "\t\tME id=%d address %02x:%02x:%02x:%02x:%02x:%02x\n",
         p->me_send->id, p->me_send->addr[0], p->me_send->addr[1],
         p->me_send->addr[2], p->me_send->addr[3], p->me_send->addr[4],
         p->me_send->addr[5]);
  printk(KERN_INFO "\tPEERS we connect to\n");
  int i;
  for (i = 0; i < p->peers_count; i++) {
    printk(KERN_INFO "\t\tid=%d address %02x:%02x:%02x:%02x:%02x:%02x\n",
           p->peers[i]->id, p->peers[i]->addr[0], p->peers[i]->addr[1],
           p->peers[i]->addr[2], p->peers[i]->addr[3], p->peers[i]->addr[4],
           p->peers[i]->addr[5]);
  }

  printk(KERN_INFO
         "\tCLIENTS we receive connections \n\t(will be updated as message are "
         "received)\n");

  for (i = 0; i < p->clients_count; i++) {
    printk(KERN_INFO "\t\tid=%d address %02x:%02x:%02x:%02x:%02x:%02x\n",
           p->clients[i]->id, p->clients[i]->addr[0], p->clients[i]->addr[1],
           p->clients[i]->addr[2], p->clients[i]->addr[3],
           p->clients[i]->addr[4], p->clients[i]->addr[5]);
  }
}

int peer_get_id(struct peer *p) { return p->id; }

void add_or_update_client(eth_address *addr, struct peers *p) {
  int i;
  for (i = 0; i < p->clients_count; ++i) {
    if (memcmp(addr, p->clients[i]->addr, eth_size) == 0) {
      return;
    }
  }
  paxos_log_info("Added a new client, now %d clients", p->clients_count + 1);
  p->clients =
      prealloc(p->clients, sizeof(struct peer) * (p->clients_count + 1));
  p->clients[p->clients_count] = make_peer(p, p->clients_count, addr);
  p->clients_count++;
}

void peer_send_del(struct peer *p, void *arg) {
  send_paxos_learner_del(p->peers->dev, get_addr(p), NULL);
}

struct net_device *get_dev(struct peers *p) {
  return p->dev;
}

int peers_missing_ok(struct peers *p) {
  return (ok_received != p->peers_count);
}

void peers_update_ok(struct peers *p, eth_address *addr) {
  int i;
  for (i = 0; i < p->peers_count; ++i) {
    if (memcmp(addr, p->peers[i]->addr, eth_size) == 0 &&
        peers_received_ok[p->peers[i]->id] == 0) {
      paxos_log_debug("peers_received_ok[%d] = 1", p->peers[i]->id);
      peers_received_ok[p->peers[i]->id] = 1;
      ok_received++;
      break;
    }
  }
}

void peers_delete_learner(struct peers *p, eth_address *addr) {
  int i, j;
  for (i = 0; i < p->clients_count; ++i) {
    if (memcmp(addr, p->clients[i]->addr, eth_size) == 0) {
      kfree(p->clients[i]);
      for (j = i; j < p->clients_count - 1; ++j) {
        p->clients[j] = p->clients[j + 1];
        p->clients[j]->id = j;
      }
      p->clients_count--;
      p->clients =
          prealloc(p->clients, sizeof(struct peer *) * (p->clients_count));
      break;
    }
  }
}

// int peers_listen(struct peers *p, udp_service *k) {
//   int ret, first_time = 0;
//   eth_address address;
//   unsigned char *in_buf = pmalloc(MAX_UDP_SIZE);
//   struct peer tmp;
//
// #if HANDLE_BIG_PKG
//   unsigned char *bigger_buff = NULL;
//   int n_packet_toget = 0, size_bigger_buf = 0;
// #endif
//
//   paxos_log_debug("%s Listening", k->name);
//   unsigned long long time_passed[N_TIMER];
//
//   unsigned long long temp, first;
//   struct timeval t1, t2;
//
//   do_gettimeofday(&t1);
//   first = timeval_to_jiffies(&t1);
//   int i;
//   for (i = 0; i < N_TIMER; i++) {
//     time_passed[i] = first;
//   }
//
//   while (1) {
//
//     if (kthread_should_stop() || signal_pending(current)) {
//       paxos_log_debug("Stopped!");
//       if (k->timer_cb[LEA_TIM] != NULL)
//         peers_foreach_acceptor(p, peer_send_del, NULL);
//
//       check_sock_allocation(k, p->sock_send, &k->socket_allocated);
//       kfree(in_buf);
//       kfree(peers_received_ok);
//
//       return 0;
//     }
//
//     memset(in_buf, '\0', MAX_UDP_SIZE);
//     memset(&address, 0, sizeof(eth_address));
//     ret = udp_server_receive(p->sock_send, &address, in_buf, MSG_WAITALL, k);
//     if (ret > 0) {
//       if (first_time == 0) {
//         memcpy(&tmp.addr, &address, sizeof(eth_address));
//         tmp.peers = p;
//         if (k->timer_cb[ACC_TIM] != NULL) {
//           add_or_update_client(&address, p);
//         }
//         ret = on_read(in_buf, &tmp, MAX_UDP_SIZE);
//       }
//     }
//
//     do_gettimeofday(&t2);
//     temp = timeval_to_jiffies(&t2);
//
//     for (i = 0; i < N_TIMER; i++) {
//       if (k->timer_cb[i] != NULL) {
//         if ((temp - time_passed[i]) >= k->timeout_jiffies[i]) {
//           time_passed[i] = temp;
//           k->timer_cb[i](k->data[i]);
//         }
//       }
//     }
//   }
//   return 1;
// }

void peers_subscribe(struct peers *p, paxos_message_type type, peer_cb cb,
                     void *arg) {
  int num = type;
  char hex[5];
  sprintf(hex, "%x", num);
  eth_listen(p->dev, (uint16_t)*hex, cb, arg);
}

static struct peer *make_peer(struct peers *peers, int id, eth_address *addr) {
  struct peer *p = pmalloc(sizeof(struct peer));
  p->id = id;
  memcpy(p->addr, addr, eth_size);
  p->peers = peers;
  return p;
}

static void free_all_peers(struct peer **p, int count) {
  int i;
  for (i = 0; i < count; i++)
    free_peer(p[i]);
  if (count > 0)
    kfree(p);
}

static void free_peer(struct peer *p) { kfree(p); }
