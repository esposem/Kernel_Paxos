#include <asm/atomic.h>
#include <net/sock.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include "kernel_udp.h"

void check_params(int * len, udp_service * k);

u32 create_address(u8 *ip)
{
  u32 addr = 0;
  int i;

  for(i=0; i<4; i++)
  {
    addr += ip[i];
    if(i==3)
    break;
    addr <<= 8;
  }
  return addr;
}

void check_sock_allocation(udp_service * k, struct socket * s){
  if(atomic_read(&k->socket_allocated) == 1){
    // printk(KERN_INFO "%s Released socket",k->name);
    atomic_set(&k->socket_allocated, 0);
    sock_release(s);
  }
}

int udp_server_send(struct socket *sock, struct sockaddr_in *address, const char *buf, const size_t length, unsigned long flags, \
   char * MODULE_NAME)
{
    struct msghdr msg;
    struct kvec vec;
    int lenn, written = 0, left =length;
    mm_segment_t oldmm;

    msg.msg_name    = address;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags;

    oldmm = get_fs(); set_fs(KERNEL_DS);

    repeat_send:
    vec.iov_len = left;
    vec.iov_base = (char *)buf + written;

    lenn = kernel_sendmsg(sock, &msg, &vec, left, left);

    if((lenn == -ERESTARTSYS) || (!(flags & MSG_WAITALL) && (lenn == -EAGAIN))){
      goto repeat_send;
    }

    if(lenn > 0)
    {
      written += lenn;
      left -= lenn;
      if(left){
        printk(KERN_INFO "%s Sent only a piece, remaining %d [udp_server_send]", MODULE_NAME, left );
        goto repeat_send;
      }
    }

    set_fs(oldmm);
    printk(KERN_INFO "%s Sent message to %pI4 : %hu [udp_server_send]",MODULE_NAME, &address->sin_addr, ntohs(address->sin_port));

    return written?written:lenn;
}

int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf,int size, unsigned long flags,\
    udp_service * k)
{
  struct msghdr msg;
  struct kvec vec;
  int lenm;

  msg.msg_name = address;
  msg.msg_namelen = sizeof(struct sockaddr_in);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = flags;

  vec.iov_len = size;
  vec.iov_base = buf;

  lenm = -EAGAIN;
  while(lenm == -ERESTARTSYS || lenm == -EAGAIN){
    if(kthread_should_stop() || signal_pending(current)){
      check_sock_allocation(k, sock);
      return 0;
    }else{
      // TODO test size
      lenm = kernel_recvmsg(sock, &msg, &vec, size, size, flags);
      if(lenm > 0){
        address = (struct sockaddr_in *) msg.msg_name;
        printk(KERN_INFO "%s Received message from %pI4 : %hu saying %s [udp_server_receive]",k->name,&address->sin_addr, ntohs(address->sin_port), buf);
      }
    }
  }

  return lenm;
}

// TODO: add struct and serialize it, void * data. Check for how much data has been sent
void _send_message(struct socket * s, struct sockaddr_in * a, unsigned char * buff, int p, char * data, int len, char * MODULE_NAME ){
  a->sin_family = AF_INET;
  a->sin_port = htons(p);
  memset(buff, '\0', len);
  strncat(buff, data, len-1);
  udp_server_send(s, a, buff,strlen(buff), MSG_WAITALL, MODULE_NAME);
}

void udp_server_init(udp_service * k, struct socket ** s, unsigned char * myip, int * myport){
  int server_err;
  struct socket *conn_socket;
  struct sockaddr_in server;
  struct timeval tv;

  server_err = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, s);
  if(server_err < 0){
    printk(KERN_INFO "%s Error %d while creating socket [udp_server_listen]",k->name, server_err);
    atomic_set(&k->thread_running, 0);
    return;
  }else{
    atomic_set(&k->socket_allocated, 1);
    printk(KERN_INFO "%s Created socket [udp_server_listen]",k->name);
  }

  conn_socket = *s;
  server.sin_addr.s_addr = htonl(create_address(myip));
  server.sin_family = AF_INET;
  server.sin_port = htons(*myport);

  server_err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&server, sizeof(server));
  if(server_err < 0) {
    printk(KERN_INFO "%s Error %d while binding socket [udp_server_listen]",k->name, server_err);
    atomic_set(&k->socket_allocated, 0);
    sock_release(conn_socket);
    atomic_set(&k->thread_running, 0);
    return;
  }else{
    printk(KERN_INFO "%s Socket is bind to %d.%d.%d%d [udp_server_listen]",k->name, *myip, *(myip +1), *(myip + 2), *(myip + 3) );
  }

  tv.tv_sec = 0;
  tv.tv_usec = MAX_RCV_WAIT;
  kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVTIMEO, (char * )&tv, sizeof(tv));
}

void check_params(int * len, udp_service * k){
  if(*len < 0 || *len > MAX_UDP_SIZE){
    printk(KERN_INFO "%s Wrong len, using default one", k->name);
    *len = 50;
  }
  (*len)++;
}

void init_service(udp_service * k, char * name, int * len){
  memset(k, 0, sizeof(udp_service));
  atomic_set(&k->socket_allocated, 0);
  atomic_set(&k->thread_running, 0);
  size_t stlen = strlen(name) + 1;
  k->name = kmalloc(stlen, GFP_KERNEL);
  memcpy(k->name, name, stlen);
  printk(KERN_INFO  "%s Initialized", k->name);
  check_params(len, k);
}

void udp_server_quit(udp_service * k, struct socket * s){
  int ret;
  if(k!= NULL){
    if(atomic_read(&k->thread_running) == 1){
      atomic_set(&k->thread_running, 0);
      if((ret = kthread_stop(k->u_thread)) == 0){
        printk(KERN_INFO "%s Terminated thread [network_server_exit]", k->name);
      }else{
        printk(KERN_INFO "%s Error %d in terminating thread [network_server_exit]", k->name, ret);
      }
    }else{
      printk(KERN_INFO "%s Thread was not running [network_server_exit]", k->name);
    }

    if(atomic_read(&k->socket_allocated) == 1){
      atomic_set(&k->socket_allocated, 0);
      sock_release(s);
      printk(KERN_INFO "%s Released socket [network_server_exit]", k->name);
    }
    printk(KERN_INFO "%s Module unloaded [network_server_exit]", k->name);
    kfree(k->name);
    kfree(k);
  }else{
    printk(KERN_INFO "Module was NULL, terminated");
  }

}
