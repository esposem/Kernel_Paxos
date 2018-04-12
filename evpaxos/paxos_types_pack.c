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
#include "eth.h"

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

static int
check_len(int len, int size)
{
  int l = ETH_DATA_LEN - size;
  if (len > l)
    len = l;
  return len;
}

static long
msgpack_pack_paxos_prepare(msgpack_packer* p, paxos_prepare* v)
{
  cp_int_packet(v->iid, &p);
  cp_int_packet(v->ballot, &p);

  return (sizeof(uint32_t) * 2);
}

static void
msgpack_unpack_paxos_prepare(msgpack_packer* o, paxos_prepare* v)
{
  dcp_int_packet(&v->iid, &o);
  dcp_int_packet(&v->ballot, &o);
}

static long
msgpack_pack_paxos_promise(msgpack_packer* p, paxos_promise* v)
{
  char* value = v->value.paxos_value_val;
  long  size = (sizeof(uint32_t) * 5);
  int   len = check_len(v->value.paxos_value_len, size);

  cp_int_packet(v->aid, &p);
  cp_int_packet(v->iid, &p);
  cp_int_packet(v->ballot, &p);
  cp_int_packet(v->value_ballot, &p);
  cp_int_packet(len, &p);

  memcpy(p, value, len);

  return size + len;
}

static void
msgpack_unpack_paxos_promise(msgpack_packer* o, paxos_promise* v)
{
  int size;

  dcp_int_packet(&v->aid, &o);
  dcp_int_packet(&v->iid, &o);
  dcp_int_packet(&v->ballot, &o);
  dcp_int_packet(&v->value_ballot, &o);
  dcp_int_packet(&size, &o);

  v->value.paxos_value_len = size;
  // if (size <= 0)
  //   return;
  // v->value.paxos_value_val = pmalloc(size);
  memcpy(v->value.paxos_value_val, o, size);
}

static long
msgpack_pack_paxos_accept(msgpack_packer* p, paxos_accept* v)
{
  long  size = (sizeof(unsigned int) * 3);
  int   len = check_len(v->value.paxos_value_len, size);
  char* value = v->value.paxos_value_val;

  cp_int_packet(v->iid, &p);
  cp_int_packet(v->ballot, &p);
  cp_int_packet(len, &p);

  memcpy(p, value, len);
  return size + len;
}

static void
msgpack_unpack_paxos_accept(msgpack_packer* o, paxos_accept* v)
{
  int size;

  dcp_int_packet(&v->iid, &o);
  dcp_int_packet(&v->ballot, &o);
  dcp_int_packet(&size, &o);

  v->value.paxos_value_len = size;
  // if (size <= 0)
  //   return;
  // v->value.paxos_value_val = pmalloc(size);
  memcpy(v->value.paxos_value_val, o, size);
}

static long
msgpack_pack_paxos_accepted(msgpack_packer* p, paxos_accepted* v)
{
  long  size = (sizeof(unsigned int) * 5);
  char* value = v->value.paxos_value_val;
  int   len = check_len(v->value.paxos_value_len, size);

  cp_int_packet(v->aid, &p);
  cp_int_packet(v->iid, &p);
  cp_int_packet(v->ballot, &p);
  cp_int_packet(v->value_ballot, &p);
  cp_int_packet(len, &p);

  memcpy(p, value, len);
  return size + len;
}

static void
msgpack_unpack_paxos_accepted(msgpack_packer* o, paxos_accepted* v)
{
  int size;

  dcp_int_packet(&v->aid, &o);
  dcp_int_packet(&v->iid, &o);
  dcp_int_packet(&v->ballot, &o);
  dcp_int_packet(&v->value_ballot, &o);
  dcp_int_packet(&size, &o);

  v->value.paxos_value_len = size;
  // if (size <= 0)
  //   return;
  // v->value.paxos_value_val = pmalloc(size);
  memcpy(v->value.paxos_value_val, o, size);
}

static long
msgpack_pack_paxos_preempted(msgpack_packer* p, paxos_preempted* v)
{
  cp_int_packet(v->aid, &p);
  cp_int_packet(v->iid, &p);
  cp_int_packet(v->ballot, &p);
  return (sizeof(uint32_t) * 3);
}

static void
msgpack_unpack_paxos_preempted(msgpack_packer* o, paxos_preempted* v)
{
  dcp_int_packet(&v->aid, &o);
  dcp_int_packet(&v->iid, &o);
  dcp_int_packet(&v->ballot, &o);
}

static long
msgpack_pack_paxos_repeat(msgpack_packer* p, paxos_repeat* v)
{
  cp_int_packet(v->from, &p);
  cp_int_packet(v->to, &p);
  return (sizeof(uint32_t) * 2);
}

static void
msgpack_unpack_paxos_repeat(msgpack_packer* o, paxos_repeat* v)
{
  dcp_int_packet(&v->from, &o);
  dcp_int_packet(&v->to, &o);
}

static long
msgpack_pack_paxos_trim(msgpack_packer* p, paxos_trim* v)
{
  cp_int_packet(v->iid, &p);
  return sizeof(uint32_t);
}

static void
msgpack_unpack_paxos_trim(msgpack_packer* o, paxos_trim* v)
{
  dcp_int_packet(&v->iid, &o);
}

static long
msgpack_pack_paxos_acceptor_state(msgpack_packer* p, paxos_acceptor_state* v)
{
  cp_int_packet(v->aid, &p);
  cp_int_packet(v->trim_iid, &p);
  return sizeof(uint32_t) * 2;
}

static void
msgpack_unpack_paxos_acceptor_state(msgpack_packer* o, paxos_acceptor_state* v)
{
  dcp_int_packet(&v->aid, &o);
  dcp_int_packet(&v->trim_iid, &o);
}

static long
msgpack_pack_paxos_client_value(msgpack_packer* p, paxos_client_value* v)
{
  char* value = v->value.paxos_value_val;
  long  size = sizeof(uint32_t);
  int   len = check_len(v->value.paxos_value_len, size);
  cp_int_packet(len, &p);
  memcpy(p, value, len);
  return size + len;
}

static void
msgpack_unpack_paxos_client_value(msgpack_packer* o, paxos_client_value* v)
{
  int size;

  dcp_int_packet(&size, &o);
  v->value.paxos_value_len = size;
  // if (size <= 0)
  //   return;
  // v->value.paxos_value_val = pmalloc(size);
  memcpy(v->value.paxos_value_val, o, size);
}

static long
msgpack_pack_paxos_learner(msgpack_packer* p, paxos_message_type enum_type)
{
  cp_int_packet(enum_type, &p);
  return sizeof(paxos_message_type);
}

static void
msgpack_unpack_paxos_learner(msgpack_packer* o, paxos_message_type enum_type)
{}

long
msgpack_pack_paxos_message(msgpack_packer* p, paxos_message* v)
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

#define ADJ_POINTER(msg, name, dest)                                           \
  ({ msg->u.name.value.paxos_value_val = dest; })

int
msgpack_unpack_paxos_message(paxos_message* v, char* v_data,
                             paxos_message_type p, msgpack_packer* o, int size)
{
  v->type = p;

  switch (p) {

    case PAXOS_CLIENT_VALUE:
      ADJ_POINTER(v, client_value, v_data);
      msgpack_unpack_paxos_client_value(o, &v->u.client_value);
      break;

    case PAXOS_PROMISE:
      ADJ_POINTER(v, promise, v_data);
      msgpack_unpack_paxos_promise(o, &v->u.promise);
      break;

    case PAXOS_ACCEPT:
      ADJ_POINTER(v, accept, v_data);
      msgpack_unpack_paxos_accept(o, &v->u.accept);
      break;

    case PAXOS_ACCEPTED:
      ADJ_POINTER(v, accepted, v_data);
      msgpack_unpack_paxos_accepted(o, &v->u.accepted);
      break;

    case PAXOS_PREPARE:
      msgpack_unpack_paxos_prepare(o, &v->u.prepare);
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

    case PAXOS_LEARNER_HI:
      msgpack_unpack_paxos_learner(o, PAXOS_LEARNER_HI);
      break;

    case PAXOS_LEARNER_DEL:
      msgpack_unpack_paxos_learner(o, PAXOS_LEARNER_DEL);
      break;

    case PAXOS_ACCEPTOR_OK:
      msgpack_unpack_paxos_learner(o, PAXOS_ACCEPTOR_OK);
      break;

    default:
      break;
  }
  return 0;
}
