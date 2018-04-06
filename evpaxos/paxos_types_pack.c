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

/*
 #ifndef _BIG_ENDIAN
 #else

 #endif
 */

#include "paxos_types_pack.h"
#include <linux/slab.h>

#ifndef _BIG_ENDIAN
// Machine is little endian
static void
cp_int_packet(uint32_t n, unsigned char** buffer)
{
  unsigned char* res = (unsigned char*)&n;
  (*buffer)[0] = res[3];
  (*buffer)[1] = res[2];
  (*buffer)[2] = res[1];
  (*buffer)[3] = res[0];
  *buffer += sizeof(uint32_t);
}

static void
dcp_int_packet(uint32_t* intres, unsigned char** buffer)
{
  unsigned char* res = (unsigned char*)intres;
  res[0] = (*buffer)[3];
  res[1] = (*buffer)[2];
  res[2] = (*buffer)[1];
  res[3] = (*buffer)[0];
  *buffer += sizeof(uint32_t);
}

#else
// Machine is big endian
static void
cp_int_packet(uint32_t n, unsigned char** buffer)
{
  memcpy(*buffer, &n, sizeof(uint32_t));
  *buffer += sizeof(uint32_t);
}

static void
dcp_int_packet(uint32_t* n, unsigned char** buffer)
{
  memcpy(n, *buffer, sizeof(uint32_t));
  *buffer += sizeof(uint32_t);
}
#endif

static long
msgpack_pack_paxos_prepare(msgpack_packer** p, paxos_prepare* v)
{
  long size = (sizeof(unsigned int) * 2);
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int iid = v->iid;
  unsigned int ballot = v->ballot;

  cp_int_packet(iid, &tmp);
  cp_int_packet(ballot, &tmp);

  return size;
}

static void
msgpack_unpack_paxos_prepare(msgpack_packer* o, paxos_prepare* v)
{
  unsigned char* buffer = (unsigned char*)o;

  dcp_int_packet(&v->iid, &buffer);
  dcp_int_packet(&v->ballot, &buffer);
}

static long
msgpack_pack_paxos_promise(msgpack_packer** p, paxos_promise* v)
{
  int  len = v->value.paxos_value_len;
  long size = (sizeof(unsigned int) * 5) + len;
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int aid = v->aid;
  unsigned int iid = v->iid;
  unsigned int ballot = v->ballot;
  unsigned int value_ballot = v->value_ballot;
  char*        value = v->value.paxos_value_val;

  cp_int_packet(aid, &tmp);
  cp_int_packet(iid, &tmp);
  cp_int_packet(ballot, &tmp);
  cp_int_packet(value_ballot, &tmp);
  cp_int_packet(len, &tmp);

  // printk("Sent data size %d, tot packet size %ld\n", len, size);
  if (size > 0)
    memcpy(tmp, value, len);
  // printk("%u %u %u %u %u %s\n", aid, iid, ballot, value_ballot, len, tmp);

  return size;
}

static int
msgpack_unpack_paxos_promise(msgpack_packer* o, paxos_promise* v,
                             int packet_len)
{
  unsigned char* buffer = (unsigned char*)o;
  int            size;

  dcp_int_packet(&v->aid, &buffer);
  dcp_int_packet(&v->iid, &buffer);
  dcp_int_packet(&v->ballot, &buffer);
  dcp_int_packet(&v->value_ballot, &buffer);
  dcp_int_packet(&size, &buffer);

  // printk("Received %d, data size %d\n", packet_len, size);

  // if ((packet_len - (sizeof(int) * 5)) != size) {
  //   printk("%s Error! packet length %d differs from supposed size of %d\n",
  //          __func__, packet_len, size);
  // }
  v->value.paxos_value_len = size;
  if (size > 0) {
    v->value.paxos_value_val = pmalloc(size);
    memset(v->value.paxos_value_val, 0, size);
    memcpy(v->value.paxos_value_val, buffer, size);
  } else {
    v->value.paxos_value_val = NULL;
  }

  // printk("%u %u %u %u %u %s\n", v->aid, v->iid, v->ballot, v->value_ballot,
  //        size, v->value.paxos_value_val);

  return 0;
}

static long
msgpack_pack_paxos_accept(msgpack_packer** p, paxos_accept* v)
{
  int  len = v->value.paxos_value_len;
  long size = (sizeof(unsigned int) * 3) + len;
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int iid = v->iid;
  unsigned int ballot = v->ballot;
  char*        value = v->value.paxos_value_val;

  cp_int_packet(iid, &tmp);
  cp_int_packet(ballot, &tmp);
  cp_int_packet(len, &tmp);

  memcpy(tmp, value, len);
  return size;
}

static int
msgpack_unpack_paxos_accept(msgpack_packer* o, paxos_accept* v, int packet_len)
{
  unsigned char* buffer = (unsigned char*)o;
  int            size;

  dcp_int_packet(&v->iid, &buffer);
  dcp_int_packet(&v->ballot, &buffer);
  dcp_int_packet(&size, &buffer);

  v->value.paxos_value_len = size;
  if (size > 0) {
    v->value.paxos_value_val = pmalloc(size);
    memcpy(v->value.paxos_value_val, buffer, size);
  } else {
    v->value.paxos_value_val = NULL;
  }

  return 0;
}

static long
msgpack_pack_paxos_accepted(msgpack_packer** p, paxos_accepted* v)
{
  int  len = v->value.paxos_value_len;
  long size = (sizeof(unsigned int) * 5) + len;
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int aid = v->aid;
  unsigned int iid = v->iid;
  unsigned int ballot = v->ballot;
  unsigned int value_ballot = v->value_ballot;
  char*        value = v->value.paxos_value_val;

  cp_int_packet(aid, &tmp);
  cp_int_packet(iid, &tmp);
  cp_int_packet(ballot, &tmp);
  cp_int_packet(value_ballot, &tmp);
  cp_int_packet(len, &tmp);

  memcpy(tmp, value, len);
  return size;
}

static int
msgpack_unpack_paxos_accepted(msgpack_packer* o, paxos_accepted* v,
                              int packet_len)
{
  unsigned char* buffer = (unsigned char*)o;
  int            size;

  dcp_int_packet(&v->aid, &buffer);
  dcp_int_packet(&v->iid, &buffer);
  dcp_int_packet(&v->ballot, &buffer);
  dcp_int_packet(&v->value_ballot, &buffer);
  dcp_int_packet(&size, &buffer);

  v->value.paxos_value_len = size;
  if (size > 0) {
    v->value.paxos_value_val = pmalloc(size);
    memcpy(v->value.paxos_value_val, buffer, size);
  } else {
    v->value.paxos_value_val = NULL;
  }

  return 0;
}

static long
msgpack_pack_paxos_preempted(msgpack_packer** p, paxos_preempted* v)
{
  long size = (sizeof(unsigned int) * 3);
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int aid = v->aid;
  unsigned int iid = v->iid;
  unsigned int ballot = v->ballot;

  cp_int_packet(aid, &tmp);
  cp_int_packet(iid, &tmp);
  cp_int_packet(ballot, &tmp);

  return size;
}

static void
msgpack_unpack_paxos_preempted(msgpack_packer* o, paxos_preempted* v)
{
  unsigned char* buffer = (unsigned char*)o;

  dcp_int_packet(&v->aid, &buffer);
  dcp_int_packet(&v->iid, &buffer);
  dcp_int_packet(&v->ballot, &buffer);
}

static long
msgpack_pack_paxos_repeat(msgpack_packer** p, paxos_repeat* v)
{
  long size = (sizeof(unsigned int) * 2);
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int from = v->from;
  unsigned int to = v->to;

  cp_int_packet(from, &tmp);
  cp_int_packet(to, &tmp);

  return size;
}

static void
msgpack_unpack_paxos_repeat(msgpack_packer* o, paxos_repeat* v)
{
  unsigned char* buffer = (unsigned char*)o;

  dcp_int_packet(&v->from, &buffer);
  dcp_int_packet(&v->to, &buffer);
}

static long
msgpack_pack_paxos_trim(msgpack_packer** p, paxos_trim* v)
{
  long size = (sizeof(unsigned int) * 1);
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int iid = v->iid;
  cp_int_packet(iid, &tmp);
  return size;
}

static void
msgpack_unpack_paxos_trim(msgpack_packer* o, paxos_trim* v)
{
  unsigned char* buffer = (unsigned char*)o;

  dcp_int_packet(&v->iid, &buffer);
}

static long
msgpack_pack_paxos_acceptor_state(msgpack_packer** p, paxos_acceptor_state* v)
{
  long size = (sizeof(unsigned int) * 2);
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  unsigned int aid = v->aid;
  unsigned int trim_iid = v->trim_iid;

  cp_int_packet(aid, &tmp);
  cp_int_packet(trim_iid, &tmp);

  return size;
}

static void
msgpack_unpack_paxos_acceptor_state(msgpack_packer* o, paxos_acceptor_state* v)
{
  unsigned char* buffer = (unsigned char*)o;

  dcp_int_packet(&v->aid, &buffer);
  dcp_int_packet(&v->trim_iid, &buffer);
}

static long
msgpack_pack_paxos_client_value(msgpack_packer** p, paxos_client_value* v)
{
  int  len = v->value.paxos_value_len;
  long size = sizeof(uint32_t) + len;
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;
  // printk("Sending size data %d tot size %ld\n", len, size);
  char* value = v->value.paxos_value_val;

  cp_int_packet(len, &tmp);

  memcpy(tmp, value, len);
  return size;
}

static int
msgpack_unpack_paxos_client_value(msgpack_packer* o, paxos_client_value* v,
                                  int packet_len)
{
  unsigned char* buffer = (unsigned char*)o;
  int            size;

  dcp_int_packet(&size, &buffer);
  // printk("Receved %d, size data %d\n", packet_len, size);

  // if ((packet_len - sizeof(int)) != size) {
  //   printk("%s Error! packet length %d differs from supposed size of %d\n",
  //          __func__, packet_len, size);
  // }
  v->value.paxos_value_len = size;
  if (size > 0) {
    v->value.paxos_value_val = pmalloc(size);
    memcpy(v->value.paxos_value_val, buffer, size);
  } else {
    v->value.paxos_value_val = NULL;
  }

  return 0;
}

static long
msgpack_pack_paxos_learner(msgpack_packer** p, paxos_message_type enum_type)
{
  long size = sizeof(unsigned int);
  *p = pmalloc(size);
  unsigned char* tmp = (unsigned char*)*p;

  paxos_message_type type = enum_type;

  cp_int_packet(type, &tmp);
  return size;
}

static int
msgpack_unpack_paxos_learner(msgpack_packer* o, paxos_message_type enum_type,
                             int packet_len)
{
  return 0;
}

long
msgpack_pack_paxos_message(msgpack_packer** p, paxos_message* v)
{
  switch (v->type) {
    case PAXOS_PREPARE:
      return msgpack_pack_paxos_prepare(p, &v->u.prepare);

    case PAXOS_PROMISE:
      return msgpack_pack_paxos_promise(p, &v->u.promise);

    case PAXOS_ACCEPT:
      return msgpack_pack_paxos_accept(p, &v->u.accept);

    case PAXOS_ACCEPTED:
      return msgpack_pack_paxos_accepted(p, &v->u.accepted);

    case PAXOS_PREEMPTED:
      return msgpack_pack_paxos_preempted(p, &v->u.preempted);

    case PAXOS_REPEAT:
      return msgpack_pack_paxos_repeat(p, &v->u.repeat);

    case PAXOS_TRIM:
      return msgpack_pack_paxos_trim(p, &v->u.trim);

    case PAXOS_ACCEPTOR_STATE:
      return msgpack_pack_paxos_acceptor_state(p, &v->u.state);

    case PAXOS_CLIENT_VALUE:
      return msgpack_pack_paxos_client_value(p, &v->u.client_value);

    case PAXOS_LEARNER_HI:
      return msgpack_pack_paxos_learner(p, PAXOS_LEARNER_HI);

    case PAXOS_LEARNER_DEL:
      return msgpack_pack_paxos_learner(p, PAXOS_LEARNER_DEL);

    case PAXOS_ACCEPTOR_OK:
      return msgpack_pack_paxos_learner(p, PAXOS_ACCEPTOR_OK);
    default:
      return 0;
  }
}

int
msgpack_unpack_paxos_message(msgpack_packer* o, paxos_message* v, int size,
                             paxos_message_type p)
{
  v->type = p;

  switch (p) {
    case PAXOS_PREPARE:
      msgpack_unpack_paxos_prepare(o, &v->u.prepare);
      return 0;

    case PAXOS_PROMISE:
      return msgpack_unpack_paxos_promise(o, &v->u.promise, size);

    case PAXOS_ACCEPT:
      return msgpack_unpack_paxos_accept(o, &v->u.accept, size);

    case PAXOS_ACCEPTED:
      return msgpack_unpack_paxos_accepted(o, &v->u.accepted, size);

    case PAXOS_PREEMPTED:
      msgpack_unpack_paxos_preempted(o, &v->u.preempted);
      return 0;

    case PAXOS_REPEAT:
      msgpack_unpack_paxos_repeat(o, &v->u.repeat);
      return 0;

    case PAXOS_TRIM:
      msgpack_unpack_paxos_trim(o, &v->u.trim);
      return 0;

    case PAXOS_ACCEPTOR_STATE:
      msgpack_unpack_paxos_acceptor_state(o, &v->u.state);
      return 0;

    case PAXOS_CLIENT_VALUE:
      return msgpack_unpack_paxos_client_value(o, &v->u.client_value, size);

    case PAXOS_LEARNER_HI:
      return msgpack_unpack_paxos_learner(o, PAXOS_LEARNER_HI, size);

    case PAXOS_LEARNER_DEL:
      return msgpack_unpack_paxos_learner(o, PAXOS_LEARNER_DEL, size);

    case PAXOS_ACCEPTOR_OK:
      return msgpack_unpack_paxos_learner(o, PAXOS_ACCEPTOR_OK, size);

    default:
      return 0;
  }
}
