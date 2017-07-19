#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <net/udp.h>
#include <asm/atomic.h>
#include <linux/time.h>
#include <net/sock.h>

MODULE_LICENSE("MIT");
MODULE_AUTHOR("Emanuele Giuseppe Esposito");

#define MODULE_NAME "Proposer"
#define MAX_RCV_WAIT 100000 // in microseconds

static int port = 3000;
module_param(port, int, S_IRUGO);
MODULE_PARM_DESC(port,"The receiving port, default 3000");

static int len = 49;
module_param(len, int, S_IRUGO);
MODULE_PARM_DESC(len,"Packet length, default 49 (automatically added space for \0)");

struct udp_proposer_service
{
  struct socket * proposer_socket;
  struct task_struct * proposer_thread;
};

static struct udp_proposer_service * udp_server;
static atomic_t released_socket = ATOMIC_INIT(0); // 0 no, 1 yes
static atomic_t thread_running = ATOMIC_INIT(0);   // 0 no, 1 yes
static atomic_t struct_allocated = ATOMIC_INIT(0); // 0 no, 1 yes
static struct in_addr clientaddr;
static unsigned char acceptorip[5] = {127,0,0,3, '\0'};


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
    int len, written = 0, left =length;
    mm_segment_t oldmm;

    msg.msg_name    = address;
    msg.msg_namelen = sizeof(struct sockaddr_in);
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = flags;
    msg.msg_flags   = 0;

    oldmm = get_fs(); set_fs(KERNEL_DS);
    printk(KERN_INFO MODULE_NAME": Sent message to %pI4 [udp_server_send]", &address->sin_addr);

    repeat_send:

    vec.iov_len = left;
    vec.iov_base = (char *)buf + written;

    // if(kthread_should_stop() || signal_pending(current)){
    //   printk(KERN_INFO MODULE_NAME": STOP [udp_server_send]");
    //   if(atomic_read(&released_socket) == 0){
    //     printk(KERN_INFO MODULE_NAME": Released socket [udp_server_send]");
    //     atomic_set(&released_socket, 1);
    //     sock_release(udp_server->proposer_socket);
    //   }
    //   return 0;
    // }

    len = kernel_sendmsg(sock, &msg, &vec, left, left);

    if((len == -ERESTARTSYS) || (!(flags & MSG_WAITALL) && (len == -EAGAIN))){
      // printk(KERN_INFO MODULE_NAME"Sent only a piece [udp_server_send]");
      goto repeat_send;
    }

    if(len > 0)
    {
      written += len;
      left -= len;
      if(left){
        // printk(KERN_INFO MODULE_NAME"Sent only a piece [udp_server_send]" );
        goto repeat_send;
      }
    }

    set_fs(oldmm);
    return written?written:len;
  }

int udp_server_receive(struct socket *sock, struct sockaddr_in *address, unsigned char *buf,int size, unsigned long flags)
{
  struct msghdr msg;
  struct kvec vec;
  int len;

  msg.msg_name = address;
  msg.msg_namelen = sizeof(struct sockaddr_in);
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = flags;

  vec.iov_len = size;
  vec.iov_base = buf;

  len = -EAGAIN;
  while(len == -ERESTARTSYS || len == -EAGAIN){
    if(kthread_should_stop() || signal_pending(current)){
      // printk(KERN_INFO MODULE_NAME": STOP [udp_server_receive]");
      if(atomic_read(&released_socket) == 0){
        printk(KERN_INFO MODULE_NAME": Released socket [udp_server_receive]");
        atomic_set(&released_socket, 1);
        sock_release(udp_server->proposer_socket);
      }
      return 0;
    }else{
      len = kernel_recvmsg(sock, &msg, &vec, size, size, flags);
      if(len > 0){
        address = (struct sockaddr_in *) msg.msg_name;
        printk(KERN_INFO MODULE_NAME": Received message from %pI4 saying %s [udp_server_receive]",&address->sin_addr, buf);
      }
    }
  }

  return len;
}

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
  struct socket *proposer_socket = udp_server->proposer_socket;

  int ret;
  unsigned char * in_buf = kmalloc(len, GFP_KERNEL);
  unsigned char * out_buf = kmalloc(len, GFP_KERNEL);
  size_t size_buf, size_msg1, size_msg2, size_msg3;


  size_msg1 = strlen("VALUE FROM CLIENT");
  size_msg2 = strlen("PROMISE 1B");
  size_msg3 = strlen("ACCEPTED 2B");

  while (1){

    if(kthread_should_stop() || signal_pending(current)){
      // printk(KERN_INFO MODULE_NAME": STOP [connection_handler]");
      if(atomic_read(&released_socket) == 0){
        printk(KERN_INFO MODULE_NAME": Released socket [connection_handler]");
        atomic_set(&released_socket, 1);
        sock_release(udp_server->proposer_socket);
      }
      kfree(in_buf);
      kfree(out_buf);
      return 0;
    }

    memset(in_buf, '\0', len);
    memset(&address, 0, sizeof(struct sockaddr_in));
    ret = udp_server_receive(proposer_socket, &address, in_buf, len, MSG_WAITALL);
    if(ret > 0){
      size_buf = strlen(in_buf);
      if(memcmp(in_buf, "VALUE FROM CLIENT", size_buf > size_msg1 ? size_msg1 : size_buf) == 0){
        // printk(KERN_INFO MODULE_NAME": Got %s from CLIENT [connection_handler]", in_buf);
        memcpy(&clientaddr, &address.sin_addr, sizeof(struct in_addr));
        address.sin_addr.s_addr = htonl(create_address(acceptorip));
        _send_message(proposer_socket, &address, out_buf, port, "PREPARE 1A");

      }else if (memcmp(in_buf, "PROMISE 1B", size_buf > size_msg2 ? size_msg2 : size_buf) == 0){
        // address.sin_addr.s_addr = htonl(create_address(acceptorip));
        _send_message(proposer_socket, &address, out_buf, port, "ACCEPT REQ 2A");

      } else if (memcmp(in_buf, "ACCEPTED 2B", size_buf > size_msg3 ? size_msg3 : size_buf) == 0){
        address.sin_addr = clientaddr;
        _send_message(proposer_socket, &address, out_buf, port, "ALL DONE");

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
  unsigned char listeningip[5] = {127,0,0,2,'\0'};

  server_err = sock_create(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &udp_server->proposer_socket);
  if(server_err < 0){
    printk(KERN_INFO MODULE_NAME": Error: %d while creating socket [udp_server_listen]", server_err);
    return 0;
  }else{
    atomic_set(&released_socket, 0);
    printk(KERN_INFO MODULE_NAME": Created socket [udp_server_listen]");
  }

  conn_socket = udp_server->proposer_socket;
  server.sin_addr.s_addr = htonl(create_address(listeningip));
  server.sin_family = AF_INET;
  server.sin_port = htons(port);

  server_err = conn_socket->ops->bind(conn_socket, (struct sockaddr*)&server, sizeof(server));
  if(server_err < 0) {
    printk(KERN_INFO MODULE_NAME": Error: %d while binding socket [udp_server_listen]", server_err);
    atomic_set(&released_socket, 1);
    sock_release(udp_server->proposer_socket);
    return 0;
  }else{
    printk(KERN_INFO MODULE_NAME": Socket is bind to 127.0.0.2 [udp_server_listen]");
  }

  tv.tv_sec = 0;
  tv.tv_usec = MAX_RCV_WAIT;
  kernel_setsockopt(conn_socket, SOL_SOCKET, SO_RCVTIMEO, (char * )&tv, sizeof(tv));

  connection_handler(NULL);
  return 0;
}

void udp_server_start(void){
  udp_server->proposer_thread = kthread_run((void *)udp_server_listen, NULL, MODULE_NAME);
  if(udp_server->proposer_thread >= 0){
    atomic_set(&thread_running,1);
    printk(KERN_INFO MODULE_NAME ": Thread running [udp_server_start]");
  }else{
    printk(KERN_INFO MODULE_NAME ": Error in starting thread. Terminated [udp_server_start]");
  }
}

static int __init network_server_init(void)
{
  atomic_set(&released_socket, 1);
  udp_server = kmalloc(sizeof(struct udp_proposer_service), GFP_KERNEL);
  if(!udp_server){
    printk(KERN_INFO MODULE_NAME": Failed to initialize server [network_server_init]");
  }else{
    atomic_set(&struct_allocated,1);
    memset(udp_server, 0, sizeof(struct udp_proposer_service));
    printk(KERN_INFO MODULE_NAME ": Server initialized [network_server_init]");
    udp_server_start();
  }
  return 0;
}

static void __exit network_server_exit(void)
{
  int ret;
  if(atomic_read(&struct_allocated) == 1){

    if(atomic_read(&thread_running) == 1){
      if((ret = kthread_stop(udp_server->proposer_thread)) == 0){
        printk(KERN_INFO MODULE_NAME": Terminated thread [network_server_exit]");
      }else{
        printk(KERN_INFO MODULE_NAME": Error %d in terminating thread [network_server_exit]", ret);
      }
    }else{
      printk(KERN_INFO MODULE_NAME": Thread was not running [network_server_exit]");
    }

    if(atomic_read(&released_socket) == 0){
      atomic_set(&released_socket, 1);
      sock_release(udp_server->proposer_socket);
      printk(KERN_INFO MODULE_NAME": Released socket [network_server_exit]");
    }

    kfree(udp_server);
  }else{
    printk(KERN_INFO MODULE_NAME": Struct was not allocated [network_server_exit]");
  }

  printk(KERN_INFO MODULE_NAME": Module unloaded [network_server_exit]");
}

module_init(network_server_init)
module_exit(network_server_exit)
