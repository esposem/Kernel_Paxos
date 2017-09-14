#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user_udp.h"
#include "paxos_types.h"

#if 0
	#define PORT 5003
	#define PROP_IP "127.0.0.3"
#else
	#define PORT 3002
	#define PROP_IP "127.0.0.2"
#endif

struct sockaddr_in address_to_sockaddr(const char *ip, int port) {
  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(struct sockaddr_in));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr(ip);
  return addr;
}

static void serialize_int_to_big(unsigned int * n, unsigned char ** buffer){
	(*buffer)[0] = *n >> 24;
  (*buffer)[1] = *n >> 16;
  (*buffer)[2] = *n >> 8;
  (*buffer)[3] = *n;
  *buffer += sizeof(unsigned int);
}

static void cp_int_packet(unsigned int * n, unsigned char ** buffer){
	memcpy(*buffer, n, sizeof(unsigned int));
	*buffer+=sizeof(unsigned int);
}

// returns 1 if architecture is little endian, 0 in case of big endian.
static int check_for_endianness(){
  unsigned int x = 1;
  char *c = (char*) &x;
  return (int)*c;
}

void udp_send_msg(struct client_value * clv, size_t size)
{
  unsigned char * packer;
  unsigned int len = size;
  long size2 = (sizeof(unsigned int) * 2) + len;
  packer = malloc(size2);
  unsigned char * tmp = packer;
  unsigned int type = PAXOS_CLIENT_VALUE;
  char * value = (char *)clv;
  if(check_for_endianness()){
    // Machine is little endian, transform the packet data from little
		// to big endian
    serialize_int_to_big(&type, &tmp);
    serialize_int_to_big(&len, &tmp);
  }else{
    cp_int_packet(&type, &tmp);
    cp_int_packet(&len, &tmp);
  }
  memcpy(tmp, value, len);
  if(cl->socket != -1){
    if (sendto(cl->socket, packer, size2, 0,(struct sockaddr *) &cl->prop_addr, sizeof(cl->prop_addr))==-1)
      perror("sendto()");
  }
  free(packer);
}


void init_socket(struct client * c){
	if ((c->socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1){
		printf("Socket not working\n");
		free(c);
		free(c->send_buffer);
		exit(1);
	}
	memset((char *) &c->prop_addr, 0, sizeof(cl->prop_addr));
	c->prop_addr.sin_family = AF_INET;
	c->prop_addr.sin_port = htons(PORT);
	if (inet_aton(PROP_IP, &cl->prop_addr.sin_addr)==0) {
		fprintf(stderr, "inet_aton() failed\n");
		free(c);
		exit(1);
	}
	struct sockaddr_in si_me;
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = 0;
	// inet_aton("127.0.0.8", &si_me.sin_addr);
	si_me.sin_addr.s_addr = htonl(0);

	if (bind(c->socket, (struct sockaddr *)&si_me, sizeof(si_me))==-1)
		perror("bind");
	struct sockaddr_in address;
	socklen_t i = (socklen_t) sizeof(struct sockaddr_in);
	getsockname(c->socket, (struct sockaddr *) &address, &i);
	printf("Socket is bind to %s : %d \n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
}
