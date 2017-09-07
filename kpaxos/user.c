#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "paxos_types.h"
#include "kernel_client.h"
#include "user.h"

#if 0
	#define PORT 5003
	#define PROP_IP "127.0.0.3"
#else
	#define PORT 3002
	#define PROP_IP "127.0.0.2"
#endif

void serialize_int_to_big(unsigned int * n, unsigned char ** buffer){
	(*buffer)[0] = *n >> 24;
  (*buffer)[1] = *n >> 16;
  (*buffer)[2] = *n >> 8;
  (*buffer)[3] = *n;
  *buffer += sizeof(unsigned int);
}

void cp_int_packet(unsigned int * n, unsigned char ** buffer){
	memcpy(*buffer, n, sizeof(unsigned int));
	*buffer+=sizeof(unsigned int);
}

// returns 1 if architecture is little endian, 0 in case of big endian.
int check_for_endianness()
{
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
    if (sendto(cl->socket, packer, size2, 0,(struct sockaddr *) &cl->si_other, sizeof(cl->si_other))==-1)
      perror("sendto()");
  }

  free(packer);
}

void init_socket(struct client * c){

	c->send_buffer = malloc(sizeof(struct client_value) + c->value_size);

	if ((c->socket=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1){
		printf("Socket not working\n");
		free(c);
		free(c->send_buffer);
		exit(1);
	}

	memset((char *) &c->si_other, 0, sizeof(cl->si_other));
	c->si_other.sin_family = AF_INET;
	c->si_other.sin_port = htons(PORT);

	if (inet_aton(PROP_IP, &cl->si_other.sin_addr)==0) {
		fprintf(stderr, "inet_aton() failed\n");
		free(c);
		exit(1);
	}

	struct sockaddr_in si_me;
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = 0;
	inet_aton("127.0.0.1", &si_me.sin_addr);
	if (bind(c->socket, (struct sockaddr *)&si_me, sizeof(si_me))==-1)
		perror("bind");
	struct sockaddr_in address;
	int i = (int) sizeof(struct sockaddr_in);
	getsockname(c->socket, (struct sockaddr *) &address, &i);
	printf("Socket is bind to %s : %d \n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
}

void open_file(struct client * c){
	if(learner_id < 0){
		printf("Error: insert learner id > 0\n");
		free(c);
		exit(1);
	}


	char * def_name = "/dev/chardevice/klearner0";
	size_t strl = strlen(def_name) + 1;
	char * filename = malloc(strl);
	memcpy(filename, def_name, strl);
	filename[strl-2] = learner_id + '0';
  c->fd = open(filename, O_RDWR | O_NONBLOCK, 0);
   if (c->fd < 0){
    perror("Failed to open the device");
		free(c);
		exit(1);
  }
}

void usage(const char* name)
{
	printf("Client Usage: %s [path/to/paxos.conf] [-h] [-o] [-v] [-p] [-c] [-id]\n", name);
	printf("  %-30s%s\n", "-h, --help", "Output this message and exit");
	printf("  %-30s%s\n", "-o, --outstanding #", "Number of outstanding client values");
	printf("  %-30s%s\n", "-v, --value-size #", "Size of client value (in bytes)");
	printf("  %-30s%s\n", "-p, --proposer-id #", "id of the proposer to connect to");
	printf("  %-30s%s\n", "-c, --client #", "if this is a client or just a learner");
	printf("  %-30s%s\n", "-id #", "the learner module id");
	exit(1);
}
