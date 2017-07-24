#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>
#include "include/acceptor.h"
// #include "include/carray.h"
// #include "include/learner.h"
// #include "include/paxos_types.h"
// #include "include/paxos.h"
// #include "include/proposer.h"
// #include "include/quorum.h"
// #include "include/storage_utils.h"
// #include "include/storage.h"

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");

#define MODULE_NAME "Acceptor: "
#define MAX_RCV_WAIT 100000 // in microseconds
#define MAX_UDP_SIZE 65507

static int port = 3000;
module_param(port, int, S_IRUGO);
MODULE_PARM_DESC(port,"The receiving port, default 3000");

static int len = 50;
module_param(len, int, S_IRUGO);
MODULE_PARM_DESC(len,"Data packet length, default 50, max 65507 (automatically added space for terminating \0)");

struct udp_acceptor_service
{
  struct socket * acceptor_socket;
  struct task_struct * acceptor_thread;
};

struct paxos_msg{
  int i;
  // char * str;
};

static struct udp_acceptor_service * kacceptor;
static atomic_t released_socket = ATOMIC_INIT(0); // 0 no, 1 yes
static atomic_t thread_running = ATOMIC_INIT(0);   // 0 no, 1 yes
static atomic_t struct_allocated = ATOMIC_INIT(0); // 0 no, 1 yes
static struct in_addr proposeraddr;
static unsigned char learnerip[5] = {127,0,0,4,'\0'};


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

int udp_server_send(struct socket *sock, struct sockaddr_in *address, const char *buf,\
  const size_t length, unsigned long flags)
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
        // printk(KERN_INFO MODULE_NAME"Sent only a piece, remaining %d [udp_server_send]", left );
        goto repeat_send;
      }
    }

    set_fs(oldmm);
    printk(KERN_INFO MODULE_NAME"Sent message to %pI4 [udp_server_send]", &address->sin_addr);

    return written?written:lenn;
  }

int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf,int size, unsigned long flags)
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
      // printk(KERN_INFO MODULE_NAME"STOP [udp_server_receive]");
      if(atomic_read(&released_socket) == 0){
        printk(KERN_INFO MODULE_NAME"Released socket [udp_server_receive]");
        atomic_set(&released_socket, 1);
        sock_release(kacceptor->acceptor_socket);
      }
      return 0;
    }else{
      lenm = kernel_recvmsg(sock, &msg, &vec, size, size, flags);
      if(lenm > 0){
        address = (struct sockaddr_in *) msg.msg_name;
        printk(KERN_INFO MODULE_NAME"Received message from %pI4 saying %s [udp_server_receive]",&address->sin_addr, buf);

      }
    }
  }

  return lenm;
}

// TODO: add struct and serialize it, void * data. Check for how much data has been sent
void _send_message(struct socket * s, struct sockaddr_in * a, unsigned char * buff, int p, char * data ){
  a->sin_family = AF_INET;
  a->sin_port = htons(p);
  memset(buff, '\0', len);
  strncat(buff, data, len-1);
  udp_server_send(s, a, buff,strlen(buff), MSG_WAITALL);
}

int connection_handler(void *data)
{
  struct sockaddr_in address;
  struct socket *accept_socket = kacceptor->acceptor_socket;
  int ret;

  unsigned char * in_buf = kmalloc(len, GFP_KERNEL);
  unsigned char * out_buf = kmalloc(len, GFP_KERNEL);
  size_t size_msg, size_msg1, size_buf;
  // struct paxos_msg message;
  // int i = 0;

  size_msg = strlen("PREPARE 1A");
  size_msg1 = strlen("ACCEPT REQ 2A");

  memset(&address, 0, sizeof(struct sockaddr_in));
  address.sin_addr.s_addr = htonl(create_address(learnerip));
  address.sin_family = AF_INET;
  address.sin_port = htons(4000);
  memset(out_buf, '\0', len);
  // while(i < len){
  //   strcat(out_buf, "1");
  //   i++;
  // }
  strcat(out_buf, "ACCEPTED 2B");

  // if(12 + sizeof(struct paxos_msg) > len){
  //   printk(KERN_INFO MODULE_NAME"Could not insert the struct, not enough space");
  // }else{
  //   message.i = 90;
  //   memcpy((out_buf + 12), &message, sizeof(struct paxos_msg));
  //   message = *((struct paxos_msg *) (out_buf + 12));
  //   printk(KERN_INFO MODULE_NAME"i = %d", message.i);
  // }

  udp_server_send(accept_socket, &address, out_buf,strlen(out_buf), MSG_WAITALL);
  // udp_server_send(accept_socket, &address, out_buf,strlen(out_buf) + sizeof(struct paxos_msg), MSG_WAITALL);

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      // printk(KERN_INFO MODULE_NAME"STOP [connection_handler]");
      if(atomic_read(&released_socket) == 0){
        printk(KERN_INFO MODULE_NAME"Released socket [connection_handler]");
        atomic_set(&released_socket, 1);
        sock_release(kacceptor->acceptor_socket);
      }
      kfree(in_buf);
      kfree(out_buf);
      return 0;
    }

    memset(in_buf, '\0', len);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(accept_socket, &address, in_buf, len, MSG_WAITALL);
    if(ret > 0){
      size_buf = strlen(in_buf);
      if(memcmp(in_buf, "PREPARE 1A", size_buf > size_msg ? size_msg : size_buf) == 0){
        memcpy(&proposeraddr, &address.sin_addr, sizeof(struct in_addr));
        // address is same as receiver
        _send_message(accept_socket, &address, out_buf, port, "PROMISE 1B");
      }else if (memcmp(in_buf, "ACCEPT REQ 2A", size_buf > size_msg1 ? size_msg1 : size_buf) == 0){
        // address is same as receiver
        _send_message(accept_socket, &address, out_buf, port, "ACCEPTED 2B");
        address.sin_addr.s_addr = htonl(create_address(learnerip));
        _send_message(accept_socket, &address, out_buf, port, "ACCEPTED 2B");
      }else{
        printk(KERN_INFO MODULE_NAME"Received %s?", in_buf);
      }
    }
  }

  return 0;
}

int udp_server_listen(void)
{
  int server_err;
  struct socket *conn_socket;
  struct sockaddr_in server;
  struct timeval tv;
  unsigned char listeningip[5] = {127,0,0,3,'\0'};

  server_err = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &kacceptor->acceptor_socket);
  if(server_err < 0){
    printk(KERN_INFO MODULE_NAME"Error: %d while creating socket [udp_server_listen]", server_err);
    atomic_set(&thread_running, 0);
    return 0;
  }else{
    atomic_set(&released_socket, 0);
    printk(KERN_INFO MODULE_NAME"Created socket [udp_server_listen]");
  }

  conn_socket = kacceptor->acceptor_socket;
  server.sin_addr.s_addr = htonl(create_address(listeningip));
  server.sin_family = AF_INET;
  server.sin_port = htons(port);

  server_err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&server, sizeof(server));
  if(server_err < 0) {
    printk(KERN_INFO MODULE_NAME"Error: %d while binding socket [udp_server_listen]", server_err);
    atomic_set(&released_socket, 1);
    sock_release(kacceptor->acceptor_socket);
    atomic_set(&thread_running, 0);
    return 0;
  }else{
    printk(KERN_INFO MODULE_NAME"Socket is bind to 127.0.0.3 [udp_server_listen]");
  }

  tv.tv_sec = 0;
  tv.tv_usec = MAX_RCV_WAIT;
  kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVTIMEO, (char * )&tv, sizeof(tv));

  connection_handler(NULL);
  atomic_set(&thread_running, 0);
  return 0;
}

void udp_server_start(void){
  kacceptor->acceptor_thread = kthread_run((void *)udp_server_listen, NULL, MODULE_NAME);
  if(kacceptor->acceptor_thread >= 0){
    atomic_set(&thread_running,1);
    printk(KERN_INFO MODULE_NAME "Thread running [udp_server_start]");
  }else{
    printk(KERN_INFO MODULE_NAME "Error in starting thread. Terminated [udp_server_start]");
  }
}

static int __init network_server_init(void)
{
  // struct acceptor * n;
  if(len < 0 || len > MAX_UDP_SIZE){
    printk(KERN_INFO MODULE_NAME"Wrong len, using default one");
    len = 50;
  }
  len++;
  atomic_set(&released_socket, 1);
  // n =  acceptor_new(3);
  // acceptor_free(n);
  kacceptor = kmalloc(sizeof(struct udp_acceptor_service), GFP_KERNEL);
  if(!kacceptor){
    printk(KERN_INFO MODULE_NAME"Failed to initialize server [network_server_init]");
  }else{
    atomic_set(&struct_allocated,1);
    memset(kacceptor, 0, sizeof(struct udp_acceptor_service));
    printk(KERN_INFO MODULE_NAME "Server initialized [network_server_init]");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  int ret;
  if(atomic_read(&struct_allocated) == 1){

    if(atomic_read(&thread_running) == 1){
      if((ret = kthread_stop(kacceptor->acceptor_thread)) == 0){
        printk(KERN_INFO MODULE_NAME"Terminated thread [network_server_exit]");
      }else{
        printk(KERN_INFO MODULE_NAME"Error %d in terminating thread [network_server_exit]", ret);
      }
    }else{
      printk(KERN_INFO MODULE_NAME"Thread was not running [network_server_exit]");
    }

    if(atomic_read(&released_socket) == 0){
      atomic_set(&released_socket, 1);
      sock_release(kacceptor->acceptor_socket);
      printk(KERN_INFO MODULE_NAME"Released socket [network_server_exit]");
    }

    kfree(kacceptor);
  }else{
    printk(KERN_INFO MODULE_NAME"Struct was not allocated [network_server_exit]");
  }

  printk(KERN_INFO MODULE_NAME"Module unloaded [network_server_exit]");
}

module_init(network_server_init)
module_exit(network_server_exit)
