#include <asm/atomic.h>
#include <net/sock.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include "kernel_udp.h"

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
        printk(KERN_INFO "%sSent only a piece, remaining %d [udp_server_send]", MODULE_NAME, left );
        goto repeat_send;
      }
    }

    set_fs(oldmm);
    printk(KERN_INFO "%sSent message to %pI4 : %hu [udp_server_send]",MODULE_NAME, &address->sin_addr, ntohs(address->sin_port));

    return written?written:lenn;
}

int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf,int size, unsigned long flags,\
    atomic_t * released_socket, char * MODULE_NAME)
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
      // printk(KERN_INFO "%sSTOP [udp_server_receive]",MODULE_NAME);
      if(atomic_read(released_socket) == 0){
        printk(KERN_INFO "%sReleased socket [udp_server_receive]",MODULE_NAME);
        atomic_set(released_socket, 1);
        sock_release(sock);
      }
      return 0;
    }else{
      // TODO test size
      lenm = kernel_recvmsg(sock, &msg, &vec, size, size, flags);
      if(lenm > 0){
        address = (struct sockaddr_in *) msg.msg_name;
        printk(KERN_INFO "%sReceived message from %pI4 : %hu saying %s [udp_server_receive]",MODULE_NAME,&address->sin_addr, ntohs(address->sin_port), buf);
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

void udp_server_quit(udp_service * k, atomic_t * struct_allocated, atomic_t * thread_running, atomic_t * released_socket, char * MODULE_NAME){
  int ret;
  if(atomic_read(struct_allocated) == 1){

    if(atomic_read(thread_running) == 1){
      if((ret = kthread_stop(k->u_thread)) == 0){
        printk(KERN_INFO "%sTerminated thread [network_server_exit]", MODULE_NAME);
      }else{
        printk(KERN_INFO "%sError %d in terminating thread [network_server_exit]", MODULE_NAME, ret);
      }
    }else{
      printk(KERN_INFO "%sThread was not running [network_server_exit]", MODULE_NAME);
    }

    if(atomic_read(released_socket) == 0){
      atomic_set(released_socket, 1);
      sock_release(k->u_socket);
      printk(KERN_INFO "%sReleased socket [network_server_exit]", MODULE_NAME);
    }

    kfree(k);
  }else{
    printk(KERN_INFO "%sStruct was not allocated [network_server_exit]",MODULE_NAME);
  }
  printk(KERN_INFO "%sModule unloaded [network_server_exit]", MODULE_NAME);

}
