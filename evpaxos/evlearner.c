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
#include "message.h"
#include "peers.h"
#include <linux/slab.h>

struct evlearner {
  struct learner *state;   /* The actual learner */
  deliver_function delfun; /* Delivery callback */
  void *delarg;            /* The argument to the delivery callback */
  struct peers *acceptors; /* Connections to acceptors */
  struct timer_list stats_ev;
  struct timeval stats_interval;
};

struct net_device *get_learn_dev(struct evlearner *ev) {
  return get_dev(ev->acceptors);
}

static void peer_send_repeat(struct net_device *dev, struct peer *p,
                             void *arg) {
  send_paxos_repeat(dev, get_addr(p), arg);
}

static void peer_send_hi(struct net_device *dev, struct peer *p, void *arg) {
  send_paxos_learner_hi(dev, get_addr(p), NULL);
}

static void evlearner_check_holes(unsigned long arg) {
  paxos_log_debug("Learner: Checking holes");
  paxos_repeat msg;
  int chunks = 10;
  struct evlearner *l = (struct evlearner *)arg;
  if (learner_has_holes(l->state, &msg.from, &msg.to)) {
    if ((msg.to - msg.from) > chunks)
      msg.to = msg.from + chunks;
    peers_foreach_acceptor(l->acceptors, peer_send_repeat, &msg);
    paxos_log_info(
        "Learner: sent PAXOS_REPEAT to all acceptors, missing %d chunks",
        (msg.to - msg.from));
  }

  if (peers_missing_ok(l->acceptors)) {
    paxos_log_info("Missing some ok, resending");
    peers_foreach_acceptor(l->acceptors, peer_send_hi, NULL);
  }
  mod_timer(&l->stats_ev, jiffies + timeval_to_jiffies(&l->stats_interval));
}

static void evlearner_deliver_next_closed(struct evlearner *l) {
  paxos_accepted deliver;
  while (learner_deliver_next(l->state, &deliver)) {
    l->delfun(deliver.iid, deliver.value.paxos_value_val,
              deliver.value.paxos_value_len, l->delarg);
    paxos_accepted_destroy(&deliver);
  }
}

/*
        Called when an accept_ack is received, the learner will update it's
   status for that instance and afterwards check if the instance is closed
*/
static void evlearner_handle_accepted(paxos_message *msg, void *arg,
                                      eth_address *src) {
  struct evlearner *l = arg;
  paxos_log_info("Learner: Received PAXOS_ACCEPTED");
  learner_receive_accepted(l->state, &msg->u.accepted);
  evlearner_deliver_next_closed(l);
}

static void evlearner_handle_ok(paxos_message *msg, void *arg,
                                eth_address *src) {
  paxos_log_info("Learner: Received PAXOS_ACCEPTOR_OK");
  struct evlearner *ev = (struct evlearner *)arg;
  peers_update_ok(ev->acceptors, src);
}

struct evlearner *evlearner_init_internal(struct evpaxos_config *config,
                                          struct peers *peers,
                                          deliver_function f, void *arg) {
  int acceptor_count = evpaxos_acceptor_count(config);
  struct evlearner *learner = pmalloc(sizeof(struct evlearner));
  if (learner == NULL)
    return NULL;

  learner->delfun = f;
  learner->delarg = arg;
  learner->state = learner_new(acceptor_count);
  learner->acceptors = peers;

  peers_subscribe(peers, PAXOS_ACCEPTED, evlearner_handle_accepted, learner);
  peers_subscribe(peers, PAXOS_ACCEPTOR_OK, evlearner_handle_ok, learner);

  paxos_log_debug("Learner: Sent HI to all acceptors");
  peers_foreach_acceptor(peers, peer_send_hi, NULL);

  setup_timer(&learner->stats_ev, evlearner_check_holes,
              (unsigned long)learner);
  learner->stats_interval = (struct timeval){0, 100000}; // 100 ms
  mod_timer(&learner->stats_ev,
            jiffies + timeval_to_jiffies(&learner->stats_interval));
  return learner;
}

struct evlearner *evlearner_init(deliver_function f, void *arg, char *if_name) {
  struct evpaxos_config *c = evpaxos_config_read();
  if (c == NULL) {
    return NULL;
  }

  struct peers *peers = peers_new(c, -1, if_name);
  if (peers == NULL) {
    return NULL;
  }
  add_acceptors_from_config(-1, peers);
  printall(peers, "Learner");

  struct evlearner *l = evlearner_init_internal(c, peers, f, arg);
  evpaxos_config_free(c);
  return l;
}

void evlearner_free_internal(struct evlearner *l) {
  learner_free(l->state);
  del_timer(&l->stats_ev);
  kfree(l);
}

void evlearner_free(struct evlearner *l) {
  printall(l->acceptors, "LEARNER");
  peers_free(l->acceptors);
  evlearner_free_internal(l);
}

void evlearner_set_instance_id(struct evlearner *l, unsigned iid) {
  learner_set_instance_id(l->state, iid);
}

static void peer_send_trim(struct net_device *dev, struct peer *p, void *arg) {
  send_paxos_trim(dev, get_addr(p), arg);
}

void evlearner_auto_trim(struct evlearner *l, unsigned iid) {
  trim_old_learn(l->state, iid);
}

void evlearner_send_trim(struct evlearner *l, unsigned iid) {
  paxos_trim trim = {iid};
  paxos_log_info("Learner: Sent PAXOS_TRIM to all acceptors");
  peers_foreach_acceptor(l->acceptors, peer_send_trim, &trim);
}
