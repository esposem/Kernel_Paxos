#include <asm/atomic.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include "paxos.h"
#include "kernel_udp.h"

static char * analyze_error(int err){
  switch (err) {
    case -98:
      return "Address already in use";
    case -97:
      return "Address family not supported by protocol";
    case -99:
      return "Cannot assign requested address";
  }
  return "not known";
}

void check_sock_allocation(udp_service * k, struct socket * s, atomic_t * allocated){
  if(atomic_read(allocated) == 1){
    printk(KERN_INFO "%s Released socket",k->name);
    atomic_set(allocated, 0);
    sock_release(s);
  }
}

// returns the number of packets sent
// need to memset buffer to \0 before
// and copy data in buffer
int udp_server_send(struct socket *sock, struct sockaddr_in * address, const char *buf, const size_t buffer_size, char * module_name)
{
    struct msghdr msg;
    struct kvec vec;
    int lenn, min, npacket =0;
    int l = buffer_size;

    mm_segment_t oldmm;

    msg.msg_name    = address;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = MSG_WAITALL;

    oldmm = get_fs(); set_fs(KERNEL_DS);

    while(l > 0){
      if(l < MAX_UDP_SIZE){
        min = l;
      }else{
        min = MAX_UDP_SIZE;
      }
      vec.iov_len = min;
      vec.iov_base = (char *)buf;
      l -= min;
      buf += min;
      npacket++;

      lenn = kernel_sendmsg(sock, &msg, &vec, min, min);
      // printk(KERN_INFO "%s Sent message to %pI4 : %hu, size %d",module_name, &address->sin_addr, ntohs(address->sin_port), lenn);
    }

    set_fs(oldmm);

    return npacket;
}

// buff MUST be MAX_UDP_SIZE big, so it can intercept any sized packet
// returns the amount of data that has received
// no need to memset buffer to \0 before
int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf, unsigned long flags,\
    udp_service * k)
{
  struct msghdr msg;
  struct kvec vec;
  int lenm;

  memset(address, 0, sizeof(struct sockaddr_in));
  memset(buf, '\0', MAX_UDP_SIZE);

  msg.msg_name = address;
  msg.msg_namelen = sizeof(struct sockaddr_in);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = flags;

  vec.iov_len = MAX_UDP_SIZE;
  vec.iov_base = buf;

  lenm = kernel_recvmsg(sock, &msg, &vec, MAX_UDP_SIZE, MAX_UDP_SIZE, flags);
  if(lenm > 0){
    address = (struct sockaddr_in *) msg.msg_name;
    // printk(KERN_INFO "%s Received message from %pI4 : %hu , size %d",k->name,&address->sin_addr, ntohs(address->sin_port), lenm);
  }

  return lenm;
}

int udp_server_init(udp_service * k, struct socket ** s, struct sockaddr_in * address, atomic_t * allocated){
  int server_err;
  struct socket *conn_socket;
  struct sockaddr_in server = *address;

  server_err = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, s);
  if(server_err < 0){
    printk(KERN_INFO "%s Error %d while creating socket",k->name, server_err);
    return -1;
  }else{
    atomic_set(allocated, 1);
    printk(KERN_INFO "%s Created socket",k->name);
  }

  conn_socket = *s;
  server.sin_family = AF_INET;

  server_err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&server, sizeof(server));
  if(server_err < 0) {
    atomic_set(allocated, 0);
    sock_release(conn_socket);
    printk(KERN_INFO "%s Error %d (%s) while binding socket with address %pI4",k->name, server_err, analyze_error(server_err), &server.sin_addr);
    return -1;
  }else{
    // update the port (might be given as 0, that is random)
    int i = (int) sizeof(struct sockaddr_in);
    inet_getname(conn_socket, (struct sockaddr *) address, &i , 0);
    printk(KERN_INFO "%s Socket is bind to %pI4 : %hu",k->name, &address->sin_addr, ntohs(address->sin_port));

    struct timeval t;
    t.tv_sec= 0;
    t.tv_usec = MAX_RCV_WAIT;
    kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVTIMEO, (char * )&t, sizeof(struct timeval));
    int k = INT_MAX;
    kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVBUF, (char * )&k, sizeof(int));
  }
  return 0;

}

void init_service(udp_service * k, char * name, int id){
  memset(k, 0, sizeof(udp_service));
  atomic_set(&k->socket_allocated, 0);
  atomic_set(&k->thread_running, 0);
  for(int i = 0; i < N_TIMER; i++){
    k->timer_cb[i] = NULL;
    k->data[i] = 0;
  }

  size_t namelen = strlen(name);
  size_t stlen = namelen + 2;

  if(id != -1){
    stlen++; // add id
  }

  k->name = kmalloc(stlen, GFP_KERNEL);
  if(id != -1){
    k->name[namelen] = (id + '0');
  }

  memcpy(k->name, name, namelen);
  k->name[stlen -2] = ':';
  k->name[stlen -1] = '\0';
}

void udp_server_quit(udp_service * k){
  int ret;
  if(k!= NULL){
    if(atomic_read(&k->thread_running) == 1){
      atomic_set(&k->thread_running, 0);
      if((ret = kthread_stop(k->u_thread)) == 0){
        printk(KERN_INFO "%s Terminated thread", k->name);
      }else{
        printk(KERN_ERR "%s Error %d in terminating thread", k->name, ret);
      }
    }else{
      printk(KERN_INFO "%s Thread was not running", k->name);
    }

    printk(KERN_INFO "%s Module unloaded", k->name);
    kfree(k->name);
    kfree(k);
  }else{
    printk(KERN_ERR "Module was NULL, terminated");
  }
}
