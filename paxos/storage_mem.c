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


#include "storage.h"
#include <linux/slab.h>
#include "uthash.h"

#ifndef HASH_FIND_IID
	#define HASH_FIND_IID(head,findint,out)                                          \
	    HASH_FIND(hh,head,findint,sizeof(iid_t),out)
#endif

#ifndef HASH_ADD_IID
	#define HASH_ADD_IID(head,intfield,add)                                          \
	    HASH_ADD(hh,head,intfield,sizeof(iid_t),add)
#endif

// KHASH_MAP_INIT_INT(record, paxos_accepted*);

struct hash_item {
	iid_t iid;
	paxos_accepted *value;
	UT_hash_handle hh;
};

struct mem_storage //db
{
	iid_t trim_iid;
	struct hash_item * record;
};


static void paxos_accepted_copy(paxos_accepted* dst, paxos_accepted* src);

static struct mem_storage*
mem_storage_new(int acceptor_id)
{
	struct mem_storage* s = kmalloc(sizeof(struct mem_storage), GFP_KERNEL);
	if (s == NULL)
		return s;
	s->trim_iid = 0;
	s->record = NULL;
	return s;
}

static int
mem_storage_open(void* handle)
{
	return 0;
}

static void
mem_storage_close(void* handle)
{
	// printk(KERN_INFO "Called mem_storage_close");
	struct mem_storage* s = handle;
	struct hash_item * current_hash;
	struct hash_item * tmp;
	// printk(KERN_ERR "there are %d in the hash",  HASH_COUNT(s->record));
 HASH_ITER(hh , s->record, current_hash, tmp) {
	  HASH_DEL(s->record, current_hash);
	  paxos_accepted_free(current_hash->value);
		kfree(current_hash);
 }
 // printk(KERN_ERR "now there are %d in the hash", HASH_COUNT(s->record));

 kfree(s);

}

static int
mem_storage_tx_begin(void* handle)
{
	return 0;
}

static int
mem_storage_tx_commit(void* handle)
{
	return 0;
}

static void
mem_storage_tx_abort(void* handle) { }

static int
mem_storage_get(void* handle, iid_t iids, paxos_accepted* out)
{
	struct mem_storage* s = handle;
	struct hash_item * h;
  HASH_FIND_IID( s->record, &iids, h);
	if(h == NULL){
		// printk(KERN_ERR "%d NOT FOUND", iids);
		return 0;
	}
	paxos_accepted_copy(out, h->value);
	// printk(KERN_ALERT "%d FOUND", iids);

  return 1;
}

static int
mem_storage_put(void* handle, paxos_accepted* acc)
{
	struct mem_storage* s = handle;
	struct hash_item * a;

	paxos_accepted* val = kmalloc(sizeof(paxos_accepted), GFP_KERNEL);
	paxos_accepted_copy(val, acc);

	HASH_FIND_IID(s->record, &(acc->iid), a);
	if (a != NULL) {
		paxos_accepted_free(a->value);
		a->value = val;
		a->iid = acc->iid;
		// printk(KERN_INFO "%d replaced", acc->iid);
	}
	else {
		a = kmalloc(sizeof(struct hash_item), GFP_KERNEL);
		a->value = val;
		a->iid = acc->iid;
		HASH_ADD_IID(s->record, iid, a);
		// printk(KERN_INFO "%d new", acc->iid);
	}


	// printk(KERN_ERR "Added %d", acc->iid);
	paxos_accepted p;
	// printk(KERN_INFO "REGETTING...");
	mem_storage_get(handle, a->iid, &p);
	// printk(KERN_INFO "--------------");

	return 0;

}

static int
mem_storage_trim(void* handle, iid_t iid)
{
	// printk(KERN_INFO "Trim called");
	struct mem_storage* s = handle;
	struct hash_item *hash_el, *tmp;
	HASH_ITER(hh, s->record, hash_el, tmp) {
		if(hash_el->iid <= (int) iid){
			HASH_DEL(s->record, hash_el);
			paxos_accepted_free(hash_el->value);
			kfree(hash_el);
		}
	}
	s->trim_iid = iid;
	return 0;
}

static iid_t
mem_storage_get_trim_instance(void* handle)
{
	struct mem_storage* s = handle;
	return s->trim_iid;
}

static void
paxos_accepted_copy(paxos_accepted* dst, paxos_accepted* src)
{
	memcpy(dst, src, sizeof(paxos_accepted));
	if (dst->value.paxos_value_len > 0) {
		dst->value.paxos_value_val = kmalloc(src->value.paxos_value_len, GFP_KERNEL);
		memcpy(dst->value.paxos_value_val, src->value.paxos_value_val,
			src->value.paxos_value_len);
	}
}

void
storage_init_mem(struct storage* s, int acceptor_id)
{
	s->handle = mem_storage_new(acceptor_id);
	s->api.open = mem_storage_open;
	s->api.close = mem_storage_close;
	s->api.tx_begin = mem_storage_tx_begin;
	s->api.tx_commit = mem_storage_tx_commit;
	s->api.tx_abort = mem_storage_tx_abort;
	s->api.get = mem_storage_get;
	s->api.put = mem_storage_put;
	s->api.trim = mem_storage_trim;
	s->api.get_trim_instance = mem_storage_get_trim_instance;
}
