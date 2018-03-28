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

#include "proposer.h"
#include "carray.h"
#include "quorum.h"
#include "uthash.h"
#include <linux/slab.h>
#include <linux/time.h>

#ifndef HASH_FIND_IID
#define HASH_FIND_IID(head, findint, out)                                      \
  HASH_FIND(hh, head, findint, sizeof(iid_t), out)
#endif

#ifndef HASH_ADD_IID
#define HASH_ADD_IID(head, intfield, add)                                      \
  HASH_ADD(hh, head, intfield, sizeof(iid_t), add)
#endif

struct instance {
  iid_t iid;
  ballot_t ballot;
  paxos_value *value;
  paxos_value *promised_value;
  ballot_t value_ballot;
  struct quorum quorum;
  struct timeval created_at;
  UT_hash_handle hh;
};

struct proposer {
  int id;
  int acceptors;
  struct carray *values;
  iid_t max_trim_iid;
  iid_t next_prepare_iid;
  struct instance *prepare_instances; /* Waiting for prepare acks */
  struct instance *accept_instances;  /* Waiting for accept acks */
};

struct timeout_iterator {
  struct instance *pi;
  struct instance *ai;
  struct timeval timeout;
  struct proposer *proposer;
};

static ballot_t proposer_next_ballot(struct proposer *p, ballot_t b);
static void proposer_preempt(struct proposer *p, struct instance *inst,
                             paxos_prepare *out);
static void proposer_move_instance(struct instance **f, struct instance **t,
                                   struct instance *inst);
static void proposer_trim_instances(struct proposer *p, struct instance **h,
                                    iid_t iid);
static struct instance *instance_new(iid_t iid, ballot_t ballot, int acceptors);
static void instance_free(struct instance *inst);
static int instance_has_value(struct instance *inst);
static int instance_has_promised_value(struct instance *inst);
static int instance_has_timedout(struct instance *inst, struct timeval *now);
static void instance_to_accept(struct instance *inst, paxos_accept *acc);
static void carray_paxos_value_free(void *v);
static int paxos_value_cmp(struct paxos_value *v1, struct paxos_value *v2);

struct proposer *proposer_new(int id, int acceptors) {
  struct proposer *p;
  p = pmalloc(sizeof(struct proposer));
  p->id = id;
  p->acceptors = acceptors;
  p->max_trim_iid = 0;
  p->next_prepare_iid = 0;
  p->values = carray_new(128);
  p->prepare_instances = NULL;
  p->accept_instances = NULL;
  return p;
}

void proposer_free(struct proposer *p) {
  struct instance *inst, *tmp;
  HASH_ITER(hh, p->prepare_instances, inst, tmp) {
    HASH_DEL(p->prepare_instances, inst);
    instance_free(inst);
  }

  HASH_ITER(hh, p->accept_instances, inst, tmp) {
    HASH_DEL(p->accept_instances, inst);
    instance_free(inst);
  }

  carray_foreach(p->values, carray_paxos_value_free);
  carray_free(p->values);
  kfree(p);
}

void proposer_propose(struct proposer *p, const char *value, size_t size) {
  paxos_value *v;
  v = paxos_value_new(value, size);
  carray_push_back(p->values, v);
}

int proposer_prepared_count(struct proposer *p) {
  return HASH_COUNT(p->prepare_instances);
}

void proposer_set_instance_id(struct proposer *p, iid_t iid) {
  if (iid > p->next_prepare_iid) {
    p->next_prepare_iid = iid;
    // remove instances older than iid
  }
  paxos_log_debug("Proposer: removing instances older than %d", iid);

  proposer_trim_instances(p, &p->prepare_instances, iid);
  proposer_trim_instances(p, &p->accept_instances, iid);
}

void proposer_prepare(struct proposer *p, paxos_prepare *out) {
  iid_t id = ++(p->next_prepare_iid);
  ballot_t bal = proposer_next_ballot(p, 0);
  struct instance *inst = NULL;
  inst = instance_new(id, bal, p->acceptors);
  HASH_ADD_IID(p->prepare_instances, iid, inst);
  *out = (paxos_prepare){inst->iid, inst->ballot};
}

int proposer_receive_promise(struct proposer *p, paxos_promise *ack,
                             paxos_prepare *out) {
  struct instance *inst = NULL;
  HASH_FIND_IID(p->prepare_instances, &ack->iid, inst);

  if (inst == NULL) {
    paxos_log_debug("Proposer: Promise dropped, instance %u not pending",
                    ack->iid);
    return 0;
  }

  if (ack->ballot < inst->ballot) {
    paxos_log_debug("Proposer: Promise dropped, too old");
    return 0;
  }

  if (ack->ballot > inst->ballot) {
    paxos_log_debug("Proposer: Instance %u preempted: ballot %d ack ballot %d",
                    inst->iid, inst->ballot, ack->ballot);
    proposer_preempt(p, inst, out);
    return 1;
  }

  if (quorum_add(&inst->quorum, ack->aid) == 0) {
    paxos_log_debug("Proposer: Duplicate promise dropped from: %d, iid: %u",
                    ack->aid, inst->iid);
    return 0;
  }

  paxos_log_debug("Proposer: Received valid promise from: %d, iid: %u",
                  ack->aid, inst->iid);

  if (ack->value.paxos_value_len > 0) {
    if (ack->value_ballot > inst->value_ballot) {
      if (instance_has_promised_value(inst)) {
        paxos_value_free(inst->promised_value);
      }
      inst->value_ballot = ack->value_ballot;
      inst->promised_value = paxos_value_new(ack->value.paxos_value_val,
                                             ack->value.paxos_value_len);
    }
  }

  return 0;
}

int proposer_accept(struct proposer *p, paxos_accept *out) {
  struct instance *inst = NULL;
  struct instance *i;

  // Find smallest inst->iid
  for (i = p->prepare_instances; i != NULL; i = i->hh.next) {
    if (inst == NULL || inst->iid > i->iid) {
      inst = i;
    }
  }

  if (inst == NULL || !quorum_reached(&inst->quorum))
    return 0;

  // paxos_log_debug("Proposer: Trying to accept iid %u", inst->iid);

  // Is there a value to accept?
  if (!instance_has_value(inst))
    inst->value = carray_pop_front(p->values);
  if (!instance_has_value(inst) && !instance_has_promised_value(inst)) {
    paxos_log_debug("Proposer: No value to accept");
    return 0;
  }

  // We have both a prepared instance and a value
  proposer_move_instance(&p->prepare_instances, &p->accept_instances, inst);
  instance_to_accept(inst, out);

  return 1;
}

int proposer_receive_accepted(struct proposer *p, paxos_accepted *ack) {
  struct instance *inst;
  HASH_FIND_IID(p->accept_instances, &ack->iid, inst);

  if (inst == NULL) {
    paxos_log_debug("Proposer: Accept ack dropped, iid: %u not pending",
                    ack->iid);
    return 0;
  }

  if (ack->ballot == inst->ballot) {
    if (!quorum_add(&inst->quorum, ack->aid)) {
      paxos_log_debug("Proposer: Duplicate accept dropped from: %d, iid: %u",
                      ack->aid, inst->iid);
      return 0;
    }

    if (quorum_reached(&inst->quorum)) {
      paxos_log_debug("Proposer: Quorum reached for instance %u", inst->iid);
      if (instance_has_promised_value(inst)) {
        if (inst->value != NULL &&
            paxos_value_cmp(inst->value, inst->promised_value) != 0) {
          carray_push_back(p->values, inst->value);
          inst->value = NULL;
        }
      }

      HASH_DEL(p->accept_instances, inst);
      paxos_log_debug("Proposer: Closed instance");
      instance_free(inst);
    }

    return 1;
  } else {
    return 0;
  }
}

int proposer_receive_preempted(struct proposer *p, paxos_preempted *ack,
                               paxos_prepare *out) {
  struct instance *inst;
  HASH_FIND_IID(p->accept_instances, &ack->iid, inst);

  if (inst == NULL) {
    paxos_log_debug("Proposer: Preempted dropped, iid: %u not pending",
                    ack->iid);
    return 0;
  }

  if (ack->ballot > inst->ballot) {
    paxos_log_debug("Proposer: Received N < prev_prop, Instance %u preempted: "
                    "ballot %d ack ballot %d",
                    inst->iid, inst->ballot, ack->ballot);
    if (instance_has_promised_value(inst))
      paxos_value_free(inst->promised_value);
    proposer_move_instance(&p->accept_instances, &p->prepare_instances, inst);
    proposer_preempt(p, inst, out);
    return 1;
  } else {
    return 0;
  }
}

void proposer_receive_acceptor_state(struct proposer *p,
                                     paxos_acceptor_state *state) {
  if (p->max_trim_iid < state->trim_iid) {
    p->max_trim_iid = state->trim_iid;
    proposer_set_instance_id(p, state->trim_iid);
  }
}

struct timeout_iterator *proposer_timeout_iterator(struct proposer *p) {
  struct timeout_iterator *iter;
  iter = pmalloc(sizeof(struct timeout_iterator));
  iter->pi = p->prepare_instances;
  iter->ai = p->accept_instances;
  iter->proposer = p;
  do_gettimeofday(&iter->timeout);
  return iter;
}

static struct instance *next_timedout(struct instance *h, struct instance *k,
                                      struct timeval *t) {
  for (; k != NULL; k = k->hh.next) {
    struct instance *inst = k;
    if (quorum_reached(&inst->quorum))
      continue;

    if (instance_has_timedout(inst, t))
      return inst;
  }

  return NULL;
}

int timeout_iterator_prepare(struct timeout_iterator *iter,
                             paxos_prepare *out) {
  struct instance *inst;
  struct proposer *p = iter->proposer;
  inst = next_timedout(p->prepare_instances, iter->pi, &iter->timeout);
  if (inst == NULL)
    return 0;
  *out = (paxos_prepare){inst->iid, inst->ballot};
  inst->created_at = iter->timeout;
  return 1;
}

int timeout_iterator_accept(struct timeout_iterator *iter, paxos_accept *out) {
  struct instance *inst;
  struct proposer *p = iter->proposer;
  inst = next_timedout(p->accept_instances, iter->ai, &iter->timeout);
  if (inst == NULL)
    return 0;
  instance_to_accept(inst, out);
  inst->created_at = iter->timeout;
  return 1;
}

void timeout_iterator_free(struct timeout_iterator *iter) { kfree(iter); }

static ballot_t proposer_next_ballot(struct proposer *p, ballot_t b) {
  if (b > 0)
    return MAX_N_OF_PROPOSERS + b;
  else
    return MAX_N_OF_PROPOSERS + p->id;
}

static void proposer_preempt(struct proposer *p, struct instance *inst,
                             paxos_prepare *out) {
  inst->ballot = proposer_next_ballot(p, inst->ballot);
  inst->value_ballot = 0;
  inst->promised_value = NULL;
  quorum_clear(&inst->quorum);
  *out = (paxos_prepare){inst->iid, inst->ballot};
  do_gettimeofday(&inst->created_at);
}

static void proposer_move_instance(struct instance **f, struct instance **t,
                                   struct instance *inst) {
  struct instance *out;
  HASH_FIND_IID(*f, &inst->iid, out);
  HASH_DEL(*f, out);
  HASH_ADD_IID(*t, iid, inst);
  quorum_clear(&inst->quorum);
}

static void proposer_trim_instances(struct proposer *p, struct instance **h,
                                    iid_t iid) {
  struct instance *i;
  for (i = *h; i != NULL; i = i->hh.next) {
    struct instance *inst = i;
    if (inst->iid <= iid) {
      if (instance_has_value(inst)) {
        carray_push_back(p->values, inst->value);
        inst->value = NULL;
      }
      HASH_DEL(*h, i);
      instance_free(inst);
    }
  }
}

static struct instance *instance_new(iid_t iid, ballot_t ballot,
                                     int acceptors) {
  struct instance *inst;
  inst = pmalloc(sizeof(struct instance));
  inst->iid = iid;
  inst->ballot = ballot;
  inst->value_ballot = 0;
  inst->value = NULL;
  inst->promised_value = NULL;
  do_gettimeofday(&inst->created_at);
  quorum_init(&inst->quorum, acceptors);
  return inst;
}

static void instance_free(struct instance *inst) {
  quorum_destroy(&inst->quorum);
  if (instance_has_value(inst))
    paxos_value_free(inst->value);
  if (instance_has_promised_value(inst))
    paxos_value_free(inst->promised_value);
  kfree(inst);
}

static int instance_has_value(struct instance *inst) {
  return inst->value != NULL;
}

static int instance_has_promised_value(struct instance *inst) {
  return inst->promised_value != NULL;
}

static int instance_has_timedout(struct instance *inst, struct timeval *now) {
  int diff = now->tv_sec - inst->created_at.tv_sec;
  return diff >= paxos_config.proposer_timeout;
}

static void instance_to_accept(struct instance *inst, paxos_accept *accept) {
  paxos_value *v = inst->value;
  if (instance_has_promised_value(inst))
    v = inst->promised_value;
  *accept = (paxos_accept){
      inst->iid, inst->ballot, {v->paxos_value_len, v->paxos_value_val}};
}

static int paxos_value_cmp(struct paxos_value *v1, struct paxos_value *v2) {
  if (v1->paxos_value_len != v2->paxos_value_len)
    return -1;
  return memcmp(v1->paxos_value_val, v2->paxos_value_val, v1->paxos_value_len);
}

static void carray_paxos_value_free(void *v) { paxos_value_free(v); }
