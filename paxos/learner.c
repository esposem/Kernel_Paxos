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


#include "learner.h"
#include "uthash.h"
#include "linux/slab.h"

#ifndef HASH_FIND_IID
	#define HASH_FIND_IID(head,findint,out)                                          \
	    HASH_FIND(hh,head,findint,sizeof(iid_t),out)
#endif

#ifndef HASH_ADD_IID
	#define HASH_ADD_IID(head,intfield,add)                                          \
	    HASH_ADD(hh,head,intfield,sizeof(iid_t),add)
#endif

struct instance
{
	iid_t iid;
	ballot_t last_update_ballot;
	paxos_accepted** acks;
	paxos_accepted* final_value;
	UT_hash_handle hh;
};

struct learner
{
	int acceptors;
	int late_start;
	iid_t current_iid;
	iid_t highest_iid_closed;
	struct instance * instances;
};

static struct instance* learner_get_instance(struct learner* l, iid_t iid);
static struct instance* learner_get_current_instance(struct learner* l);
static struct instance* learner_get_instance_or_create(struct learner* l,
	iid_t iid);
static void learner_delete_instance(struct learner* l, struct instance* inst);
static struct instance* instance_new(int acceptors);
static void instance_free(struct instance* i, int acceptors);
static void instance_update(struct instance* i, paxos_accepted* ack, int acceptors);
static int instance_has_quorum(struct instance* i, int acceptors);
static void instance_add_accept(struct instance* i, paxos_accepted* ack);
static paxos_accepted* paxos_accepted_dup(paxos_accepted* ack);
static void paxos_value_copy(paxos_value* dst, paxos_value* src);


struct learner*
learner_new(int acceptors)
{
	struct learner* l;
	l = kmalloc(sizeof(struct learner), GFP_KERNEL);
	l->acceptors = acceptors;
	l->current_iid = 1;
	l->highest_iid_closed = 1;
	l->late_start = !paxos_config.learner_catch_up;
	l->instances = NULL;
	return l;
}

void
learner_free(struct learner* l)
{
	struct instance * inst, *tmp;

 HASH_ITER(hh , l->instances, inst, tmp) {
	  HASH_DEL(l->instances, inst);
	  instance_free(inst, l->acceptors);
 }
 kfree(l);

}

void
learner_set_instance_id(struct learner* l, iid_t iid)
{
	l->current_iid = iid + 1;
	l->highest_iid_closed = iid;
}

void
learner_receive_accepted(struct learner* l, paxos_accepted* ack)
{
	if (l->late_start) {
		l->late_start = 0;
		l->current_iid = ack->iid;
	}

	if (ack->iid < l->current_iid) {
		// printk(KERN_INFO "Learner: Dropped paxos_accepted for iid %u. Already delivered.", ack->iid);
		return;
	}

	struct instance* inst;
	inst = learner_get_instance_or_create(l, ack->iid);

	instance_update(inst, ack, l->acceptors);

	if (instance_has_quorum(inst, l->acceptors) && (inst->iid > l->highest_iid_closed)){
		l->highest_iid_closed = inst->iid;
		// printk(KERN_INFO "Learner: Instance %u has a quorum and it's > highest iid closed, closing it...", inst->iid);
	}
}

int
learner_deliver_next(struct learner* l, paxos_accepted* out)
{
	struct instance* inst = learner_get_current_instance(l);

	if (inst == NULL || !instance_has_quorum(inst, l->acceptors)){
		return 0;
	}

	// printk(KERN_INFO "Learner: Deleted instance %u", inst->iid );
	memcpy(out, inst->final_value, sizeof(paxos_accepted));
	paxos_value_copy(&out->value, &inst->final_value->value);
	learner_delete_instance(l, inst);
	l->current_iid++;
	return 1;
}

int
learner_has_holes(struct learner* l, iid_t* from, iid_t* to)
{
	if (l->highest_iid_closed > l->current_iid) {
		*from = l->current_iid;
		*to = l->highest_iid_closed;
		return 1;
	}
	return 0;
}

static struct instance*
learner_get_instance(struct learner* l, iid_t iids)
{
	// // printk(KERN_INFO "Searching for %d", iids);
	struct instance * h = NULL;
  HASH_FIND_IID( l->instances, &iids, h);  /* h: output pointer */
	return h;
}

static struct instance*
learner_get_current_instance(struct learner* l)
{
	return learner_get_instance(l, l->current_iid);
}

static struct instance*
learner_get_instance_or_create(struct learner* l, iid_t iids)
{
	struct instance* inst = learner_get_instance(l, iids);
	if (inst == NULL) {
		// // printk(KERN_ERR "Instance is null, creating new one");
		inst = instance_new(l->acceptors);
		// // printk(KERN_ERR "address ad id of created value %p %d\n", inst, inst->iid);
		inst->iid = iids;
		HASH_ADD_IID(l->instances, iid, inst);
		// // printk(KERN_ERR "there are %d in hashtable", (int)HASH_COUNT(l->instances));
		struct instance * h = NULL;
		HASH_FIND_IID( l->instances, &iids, h);
		// // printk(KERN_ERR "found %p %d\n", h, iids);
		// if(h == NULL){
		// 	// printk(KERN_ERR "Instance is null, YOU HAVE A PROBLEM");
		// }
	}
	// else{
	// 	// printk(KERN_ERR "Instance is NOT null");
	// }
	return inst;
}

static void
learner_delete_instance(struct learner* l, struct instance* inst)
{
	HASH_DEL(l->instances, inst);
	instance_free(inst, l->acceptors);
}

static struct instance*
instance_new(int acceptors)
{
	int i;
	struct instance* inst;
	inst = kmalloc(sizeof(struct instance), GFP_KERNEL);
	memset(inst, 0, sizeof(struct instance));
	inst->acks = kmalloc(sizeof(paxos_accepted*) * acceptors, GFP_KERNEL);
	for (i = 0; i < acceptors; ++i)
		inst->acks[i] = NULL;
	return inst;
}

static void
instance_free(struct instance* inst, int acceptors)
{
	int i;
	for (i = 0; i < acceptors; i++)
		if (inst->acks[i] != NULL)
			paxos_accepted_free(inst->acks[i]);
	kfree(inst->acks);
	kfree(inst);
}

static void
instance_update(struct instance* inst, paxos_accepted* accepted, int acceptors)
{
	if (inst->iid == 0) {
		// printk(KERN_ERR "Learner: Received first message for iid: %u", accepted->iid);
		inst->iid = accepted->iid;
		inst->last_update_ballot = accepted->ballot;
	}

	if (instance_has_quorum(inst, acceptors)) {
		// printk(KERN_INFO "Learner: Dropped paxos_accepted iid %u. Already closed.",accepted->iid);
		return;
	}

	paxos_accepted* prev_accepted = inst->acks[accepted->aid];
	if (prev_accepted != NULL && prev_accepted->ballot >= accepted->ballot) {
		// printk(KERN_INFO " Learner: Dropped paxos_accepted for iid %u." "Previous ballot is newer or equal.", accepted->iid);
		return;
	}

	instance_add_accept(inst, accepted);
}

/*
	Checks if a given instance is closed, that is if a quorum of acceptor
	accepted the same value ballot pair.
	Returns 1 if the instance is closed, 0 otherwise.
*/
static int
instance_has_quorum(struct instance* inst, int acceptors)
{
	paxos_accepted* curr_ack;
	int i, a_valid_index = -1, count = 0;

	if (inst->final_value != NULL)
		return 1;

	for (i = 0; i < acceptors; i++) {
		curr_ack = inst->acks[i];

		// Skip over missing acceptor acks
		if (curr_ack == NULL) continue;

		// Count the ones "agreeing" with the last added
		if (curr_ack->ballot == inst->last_update_ballot) {
			count++;
			a_valid_index = i;
		}
	}

	if (count >= paxos_quorum(acceptors)) {
		// printk(KERN_INFO "Learner: Reached quorum, iid: %u is closed!", inst->iid);
		inst->final_value = inst->acks[a_valid_index];
		return 1;
	}
	return 0;
}

/*
	Adds the given paxos_accepted to the given instance,
	replacing the previous paxos_accepted, if any.
*/
static void
instance_add_accept(struct instance* inst, paxos_accepted* accepted)
{
	int acceptor_id = accepted->aid;
	if (inst->acks[acceptor_id] != NULL)
		paxos_accepted_free(inst->acks[acceptor_id]);
	inst->acks[acceptor_id] = paxos_accepted_dup(accepted);
	inst->last_update_ballot = accepted->ballot;
}

/*
	Returns a copy of it's argument.
*/
static paxos_accepted*
paxos_accepted_dup(paxos_accepted* ack)
{
	paxos_accepted* copy;
	copy = kmalloc(sizeof(paxos_accepted), GFP_KERNEL);
	memcpy(copy, ack, sizeof(paxos_accepted));
	paxos_value_copy(&copy->value, &ack->value);
	return copy;
}

static void
paxos_value_copy(paxos_value* dst, paxos_value* src)
{
	int len = src->paxos_value_len;
	dst->paxos_value_len = len;
	if (src->paxos_value_val != NULL) {
		dst->paxos_value_val = kmalloc(len, GFP_KERNEL);
		memcpy(dst->paxos_value_val, src->paxos_value_val, len);
	}
}
