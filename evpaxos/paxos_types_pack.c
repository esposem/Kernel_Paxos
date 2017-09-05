/*
 * Copyright (c) 2013-2015, University of Lugano
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


#include "paxos_types_pack.h"
#include <linux/slab.h>

void serialize_int_to_big(unsigned int * n, unsigned char ** buffer){
	(*buffer)[0] = *n >> 24;
  (*buffer)[1] = *n >> 16;
  (*buffer)[2] = *n >> 8;
  (*buffer)[3] = *n;
  *buffer += sizeof(unsigned int);
}

void deserialize_int_to_big(unsigned int * intres, unsigned char ** buffer){
  unsigned char * res = (unsigned char *) intres;
  res[0] = (*buffer)[3];
  res[1] = (*buffer)[2];
  res[2] = (*buffer)[1];
  res[3] = (*buffer)[0];
  *buffer += sizeof(unsigned int);
}

void cp_int_packet(unsigned int * n, unsigned char ** buffer){
	memcpy(*buffer, n, sizeof(unsigned int));
	*buffer+=sizeof(unsigned int);
}

void dcp_int_packet(unsigned int * n, unsigned char ** buffer){
	memcpy(n, *buffer, sizeof(unsigned int));
	*buffer+=sizeof(unsigned int);
}

long msgpack_pack_paxos_prepare(msgpack_packer** p, paxos_prepare* v)
{
	long size = (sizeof(unsigned int) * 3);
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_PREPARE;
	unsigned int iid = v->iid;
	unsigned int ballot = v->ballot;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&iid, &tmp);
		serialize_int_to_big(&ballot, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&iid, &tmp);
		cp_int_packet(&ballot, &tmp);
	#endif

	return size;
}

void msgpack_unpack_paxos_prepare(msgpack_packer* o, paxos_prepare* v)
{
	unsigned char * buffer = (unsigned char *) o;
	// buffer+=sizeof(unsigned int); //skip type
	#ifndef _BIG_ENDIAN
	// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->iid, &buffer);
		deserialize_int_to_big(&v->ballot, &buffer);
	#else
		dcp_int_packet(&v->iid, &buffer);
		dcp_int_packet(&v->ballot, &buffer);
	#endif
}

long msgpack_pack_paxos_promise(msgpack_packer** p, paxos_promise* v)
{
	int len = v->value.paxos_value_len;
	long size = (sizeof(unsigned int) * 6) + len;
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_PROMISE;
	unsigned int aid = v->aid;
	unsigned int iid = v->iid;
	unsigned int ballot = v->ballot;
	unsigned int value_ballot = v->value_ballot;
	char * value = v->value.paxos_value_val;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&aid, &tmp);
		serialize_int_to_big(&iid, &tmp);
		serialize_int_to_big(&ballot, &tmp);
		serialize_int_to_big(&value_ballot, &tmp);
		serialize_int_to_big(&len, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&aid, &tmp);
		cp_int_packet(&iid, &tmp);
		cp_int_packet(&ballot, &tmp);
		cp_int_packet(&value_ballot, &tmp);
		cp_int_packet(&len, &tmp);
	#endif
	memcpy(tmp, value, len);
	return size;
}

int msgpack_unpack_paxos_promise(msgpack_packer* o, paxos_promise* v, int packet_len)
{
	unsigned char * buffer = (unsigned char *) o;
	int size;
	#ifndef _BIG_ENDIAN
	// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->aid, &buffer);
		deserialize_int_to_big(&v->iid, &buffer);
		deserialize_int_to_big(&v->ballot, &buffer);
		deserialize_int_to_big(&v->value_ballot, &buffer);
		deserialize_int_to_big(&size, &buffer);
	#else
		dcp_int_packet(&v->aid, &buffer);
		dcp_int_packet(&v->iid, &buffer);
		dcp_int_packet(&v->ballot, &buffer);
		dcp_int_packet(&v->value_ballot, &buffer);
		dcp_int_packet(&size, &buffer);
	#endif
	v->value.paxos_value_len = size;
	// buffer now is pointing to the beginning of value.
	// check with the len if match
	// packet_len -= 5 * (sizeof (unsigned int));
	// if(size > packet_len){
	// 	memcpy(v->value.paxos_value_val, buffer,packet_len);
	// 	return size-packet_len;
	// }
	v->value.paxos_value_val = kmalloc(size, GFP_KERNEL);
	memcpy(v->value.paxos_value_val, buffer,size);
	return 0;
}

long msgpack_pack_paxos_accept(msgpack_packer** p, paxos_accept* v)
{
	int len = v->value.paxos_value_len;
	long size = (sizeof(unsigned int) * 4) + len;
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_ACCEPT;
	unsigned int iid = v->iid;
	unsigned int ballot = v->ballot;
	char * value = v->value.paxos_value_val;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&iid, &tmp);
		serialize_int_to_big(&ballot, &tmp);
		serialize_int_to_big(&len, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&iid, &tmp);
		cp_int_packet(&ballot, &tmp);
		cp_int_packet(&len, &tmp);
	#endif
	memcpy(tmp, value, len);
	return size;
}

int msgpack_unpack_paxos_accept(msgpack_packer* o, paxos_accept* v, int packet_len)
{
	unsigned char * buffer = (unsigned char *) o;
	// buffer+=sizeof(unsigned int); //skip type
	int size;
	#ifndef _BIG_ENDIAN
	// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->iid, &buffer);
		deserialize_int_to_big(&v->ballot, &buffer);
		deserialize_int_to_big(&size, &buffer);
	#else
		dcp_int_packet(&v->iid, &buffer);
		dcp_int_packet(&v->ballot, &buffer);
		dcp_int_packet(&size, &buffer);
	#endif
	v->value.paxos_value_len = size;
	// buffer now is pointing to the beginning of value.
	// check with the len if match
	// packet_len -= 3 * (sizeof (unsigned int));
	// if(size > packet_len){
	// 	memcpy(v->value.paxos_value_val, buffer,packet_len);
	// 	return size-packet_len;
	// }
	v->value.paxos_value_val = kmalloc(size, GFP_KERNEL);
	memcpy(v->value.paxos_value_val, buffer,size);
	return 0;
}

long msgpack_pack_paxos_accepted(msgpack_packer** p, paxos_accepted* v)
{
	int len = v->value.paxos_value_len;
	long size = (sizeof(unsigned int) * 6) + len;
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_ACCEPTED;
	unsigned int aid = v->aid;
	unsigned int iid = v->iid;
	unsigned int ballot = v->ballot;
	unsigned int value_ballot = v->value_ballot;
	char * value = v->value.paxos_value_val;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&aid, &tmp);
		serialize_int_to_big(&iid, &tmp);
		serialize_int_to_big(&ballot, &tmp);
		serialize_int_to_big(&value_ballot, &tmp);
		serialize_int_to_big(&len, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&aid, &tmp);
		cp_int_packet(&iid, &tmp);
		cp_int_packet(&ballot, &tmp);
		cp_int_packet(&value_ballot, &tmp);
		cp_int_packet(&len, &tmp);
	#endif
	memcpy(tmp, value, len);
	return size;
}

int msgpack_unpack_paxos_accepted(msgpack_packer* o, paxos_accepted* v, int packet_len)
{
	unsigned char * buffer = (unsigned char *) o;
	int size;
	#ifndef _BIG_ENDIAN
	// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->aid, &buffer);
		deserialize_int_to_big(&v->iid, &buffer);
		deserialize_int_to_big(&v->ballot, &buffer);
		deserialize_int_to_big(&v->value_ballot, &buffer);
		deserialize_int_to_big(&size, &buffer);
	#else
		dcp_int_packet(&v->aid, &buffer);
		dcp_int_packet(&v->iid, &buffer);
		dcp_int_packet(&v->ballot, &buffer);
		dcp_int_packet(&v->value_ballot, &buffer);
		dcp_int_packet(&size, &buffer);
	#endif
	v->value.paxos_value_len = size;
	// packet_len -= 5 * (sizeof (unsigned int));
	// if(size > packet_len){
	// 	memcpy(v->value.paxos_value_val, buffer,packet_len);
	// 	return size-packet_len;
	// }
	v->value.paxos_value_val = kmalloc(size, GFP_KERNEL);
	memcpy(v->value.paxos_value_val, buffer,size);
	return 0;
}

long msgpack_pack_paxos_preempted(msgpack_packer** p, paxos_preempted* v)
{
	long size = (sizeof(unsigned int) * 4);
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_PREEMPTED;
	unsigned int aid = v->aid;
	unsigned int iid = v->iid;
	unsigned int ballot = v->ballot;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&aid, &tmp);
		serialize_int_to_big(&iid, &tmp);
		serialize_int_to_big(&ballot, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&aid, &tmp);
		cp_int_packet(&iid, &tmp);
		cp_int_packet(&ballot, &tmp);
	#endif

	return size;
}

void msgpack_unpack_paxos_preempted(msgpack_packer* o, paxos_preempted* v)
{
	unsigned char * buffer = (unsigned char *) o;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->aid, &buffer);
		deserialize_int_to_big(&v->iid, &buffer);
		deserialize_int_to_big(&v->ballot, &buffer);
	#else
		dcp_int_packet(&v->aid, &buffer);
		dcp_int_packet(&v->iid, &buffer);
		dcp_int_packet(&v->ballot, &buffer);
	#endif
}

long msgpack_pack_paxos_repeat(msgpack_packer** p, paxos_repeat* v)
{
	long size = (sizeof(unsigned int) * 3);
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_REPEAT;
	unsigned int from = v->from;
	unsigned int to = v->to;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&from, &tmp);
		serialize_int_to_big(&to, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&from, &tmp);
		cp_int_packet(&to, &tmp);
	#endif

	return size;
}

void msgpack_unpack_paxos_repeat(msgpack_packer* o, paxos_repeat* v)
{
	unsigned char * buffer = (unsigned char *) o;
	// buffer+=sizeof(unsigned int); //skip type

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->from, &buffer);
		deserialize_int_to_big(&v->to, &buffer);
	#else
		dcp_int_packet(&v->from, &buffer);
		dcp_int_packet(&v->to, &buffer);
	#endif
}

long msgpack_pack_paxos_trim(msgpack_packer** p, paxos_trim* v)
{
	long size = (sizeof(unsigned int) * 2);
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_TRIM;
	unsigned int iid = v->iid;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&iid, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&iid, &tmp);
	#endif
	return size;
}

void msgpack_unpack_paxos_trim(msgpack_packer* o, paxos_trim* v)
{
	unsigned char * buffer = (unsigned char *) o;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->iid, &buffer);
	#else
		dcp_int_packet(&v->iid, &buffer);
	#endif
}

long msgpack_pack_paxos_acceptor_state(msgpack_packer** p, paxos_acceptor_state* v)
{
	long size = (sizeof(unsigned int) * 3);
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_ACCEPTOR_STATE;
	unsigned int aid = v->aid;
	unsigned int trim_iid = v->trim_iid;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&aid, &tmp);
		serialize_int_to_big(&trim_iid, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&aid, &tmp);
		cp_int_packet(&trim_iid, &tmp);
	#endif

	return size;
}

void msgpack_unpack_paxos_acceptor_state(msgpack_packer* o, paxos_acceptor_state* v)
{
	unsigned char * buffer = (unsigned char *) o;
	// buffer+=sizeof(unsigned int); //skip type

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->aid, &buffer);
		deserialize_int_to_big(&v->trim_iid, &buffer);
	#else
		dcp_int_packet(&v->aid, &buffer);
		dcp_int_packet(&v->trim_iid, &buffer);
	#endif
}

long msgpack_pack_paxos_client_value(msgpack_packer** p, paxos_client_value* v)
{
	int len = v->value.paxos_value_len;
	long size = (sizeof(unsigned int) * 2) + len;
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = PAXOS_CLIENT_VALUE;
	char * value = v->value.paxos_value_val;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
		serialize_int_to_big(&len, &tmp);
	#else
		cp_int_packet(&type, &tmp);
		cp_int_packet(&len, &tmp);
	#endif
	memcpy(tmp, value, len);
	return size;
}

int msgpack_unpack_paxos_client_value(msgpack_packer* o, paxos_client_value* v, int packet_len)
{
	unsigned char * buffer = (unsigned char *) o;
	int size;
	#ifndef _BIG_ENDIAN
	// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&size, &buffer);
	#else
		dcp_int_packet(&size, &buffer);
	#endif
	v->value.paxos_value_len = size;

	// buffer now is pointing to the beginning of value.
	// check with the len if match
	// packet_len -= (sizeof (unsigned int));
	// if(size > packet_len){
	// 	memcpy(v->value.paxos_value_val, buffer,packet_len);
	// 	return size-packet_len;
	// }
	v->value.paxos_value_val = kmalloc(size, GFP_KERNEL);
	memcpy(v->value.paxos_value_val, buffer,size);
	return 0;
}

long msgpack_pack_paxos_learner(msgpack_packer** p, void  * v, int enum_type){
	long size = sizeof(unsigned int);
	*p = kmalloc(size , GFP_KERNEL);
	unsigned char * tmp = (unsigned char *) *p;

	unsigned int type = enum_type;

	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet data from little to big endian
		serialize_int_to_big(&type, &tmp);
	#else
		cp_int_packet(&type, &tmp);
	#endif
	return size;
}

int msgpack_unpack_paxos_learner(msgpack_packer* o, void * v, int packet_len){
	return 0;
}

long msgpack_pack_paxos_message(msgpack_packer** p, paxos_message* v)
{
	long ret = 0;
	switch (v->type) {
	case PAXOS_PREPARE:
		ret = msgpack_pack_paxos_prepare(p, &v->u.prepare);
		break;
	case PAXOS_PROMISE:
		ret = msgpack_pack_paxos_promise(p, &v->u.promise);
		break;
	case PAXOS_ACCEPT:
		ret = msgpack_pack_paxos_accept(p, &v->u.accept);
		break;
	case PAXOS_ACCEPTED:
		ret = msgpack_pack_paxos_accepted(p, &v->u.accepted);
		break;
	case PAXOS_PREEMPTED:
		ret = msgpack_pack_paxos_preempted(p, &v->u.preempted);
		break;
	case PAXOS_REPEAT:
		ret = msgpack_pack_paxos_repeat(p, &v->u.repeat);
		break;
	case PAXOS_TRIM:
		ret = msgpack_pack_paxos_trim(p, &v->u.trim);
		break;
	case PAXOS_ACCEPTOR_STATE:
		ret = msgpack_pack_paxos_acceptor_state(p, &v->u.state);
		break;
	case PAXOS_CLIENT_VALUE:
		ret = msgpack_pack_paxos_client_value(p, &v->u.client_value);
		break;
	case PAXOS_LEARNER_HI:
		ret = msgpack_pack_paxos_learner(p, &v->u.learner_hi, PAXOS_LEARNER_HI);
		break;
	case PAXOS_LEARNER_DEL:
		ret = msgpack_pack_paxos_learner(p, &v->u.learner_del, PAXOS_LEARNER_DEL);
		break;
	case PAXOS_ACCEPTOR_OK:
		ret = msgpack_pack_paxos_learner(p, &v->u.accept_ok, PAXOS_ACCEPTOR_OK);
		break;
	}
	return ret;
}

int msgpack_unpack_paxos_message(msgpack_packer* o, paxos_message* v, int size)
{
	int partial_message = 0;
	#ifndef _BIG_ENDIAN
		// Machine is little endian, transform the packet from big to little endian
		deserialize_int_to_big(&v->type, &o);
	#else
		dcp_int_packet(&v->type, &o);
	#endif
	size -= sizeof(unsigned int);

	switch (v->type) {
	case PAXOS_PREPARE:
		msgpack_unpack_paxos_prepare(o, &v->u.prepare);
		break;
	case PAXOS_PROMISE:
		partial_message = msgpack_unpack_paxos_promise(o, &v->u.promise, size);
		break;
	case PAXOS_ACCEPT:
		partial_message = msgpack_unpack_paxos_accept(o, &v->u.accept, size);
		break;
	case PAXOS_ACCEPTED:
		partial_message = msgpack_unpack_paxos_accepted(o, &v->u.accepted, size);
		break;
	case PAXOS_PREEMPTED:
		msgpack_unpack_paxos_preempted(o, &v->u.preempted);
		break;
	case PAXOS_REPEAT:
		msgpack_unpack_paxos_repeat(o, &v->u.repeat);
		break;
	case PAXOS_TRIM:
		msgpack_unpack_paxos_trim(o, &v->u.trim);
		break;
	case PAXOS_ACCEPTOR_STATE:
		msgpack_unpack_paxos_acceptor_state(o, &v->u.state);
		break;
	case PAXOS_CLIENT_VALUE:
		partial_message = msgpack_unpack_paxos_client_value(o, &v->u.client_value, size);
		break;
	case PAXOS_LEARNER_HI:
		// partial_message = msgpack_unpack_paxos_learner(o, &v->u.learner_hi, size, PAXOS_LEARNER_HI);
		break;
	case PAXOS_LEARNER_DEL:
		// partial_message = msgpack_unpack_paxos_learner(o, &v->u.learner_del, size, PAXOS_LEARNER_DEL);
		break;
	case PAXOS_ACCEPTOR_OK:
		// partial_message = msgpack_unpack_paxos_learner(o, &v->u.accept_ok, size, PAXOS_ACCEPTOR_OK);
		break;
	}
	return partial_message;
}
