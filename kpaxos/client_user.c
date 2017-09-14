#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "user_levent.h"
#include "user_udp.h"
#include "user_stats.h"

static char receive[BUFFER_LENGTH];
struct client * cl;
static int use_chardevice = 0, cansend = 0, use_socket = 0, dest_port, trimvalue = 100000, isalearner = 0, outstanding = 1;
static char * dest_addr;
int learner_id = -1;


static void unpack_message(char * msg){
  struct user_msg * mess = (struct user_msg *) msg;
	struct client_value * val = (struct client_value *) mess->value;
  // printf("%d %d\n",val->client_id, cl->id );
	if(cansend && val->client_id == cl->id){
		update_stats(&cl->stats, val->t, cl->value_size);
		event_add(cl->resend_ev, &cl->resend_interval);

		// struct timespec t, j;
		// t.tv_sec = 0;
		// t.tv_nsec = 10000; //0.01ms
		// nanosleep(&t, &j);
		client_submit_value(cl);
	}else if (isalearner){
		// printf("On deliver iid:%d value:%.16s",mess->iid, val->value );

    if(use_socket){
      for (size_t i = 0; i < cl->s->clients_count; i++) {
        if(cl->s->connections[i]->cl_id == val->client_id){
          size_t total_size = sizeof(struct user_msg) + sizeof(struct client_value) + cl->value_size;
          bufferevent_write(cl->s->connections[i]->bev, msg, total_size);
          break;
        }
      }
    }
	}

  if(use_chardevice && mess->iid % trimvalue == 0){
    size_t s = mess->iid - trimvalue+1;
    write_file(cl->fd, &s, TRIM, sizeof(size_t), -1);
  }
}

static void on_read_file(evutil_socket_t fd, short event, void *arg) {
  int len;
  struct event *ev = arg;
  len = read(cl->fd, receive, BUFFER_LENGTH);

  if (len < 0) {
    if (len == -2){
			printf("Stopped by kernel module\n");
      event_del(ev);
      event_base_loopbreak(event_get_base(ev));
    }
    return;
  }
  unpack_message(receive);
}

void on_read_sock(struct bufferevent *bev, void *arg){
  struct connection * conn = (struct connection *) arg;
  char * c = malloc(BUFFER_LENGTH);
  char ok = 'k';
  size_t len = bufferevent_read(bev, c, BUFFER_LENGTH);
  if (len) {
    if(isalearner){
      memcpy(&conn->cl_id, c, sizeof(int));
      bufferevent_write(conn->bev, &ok, 1);
    }else{
      if(memcmp(c, &ok, 1) == 0){
        for (size_t i = 0; i < outstanding; i++) {
          client_submit_value(cl);
        }
      }else{
        unpack_message(c);
      }
    }
  }
  free(c);
}


static void
on_resend(evutil_socket_t fd, short event, void *arg)
{
	client_submit_value(cl);
	event_add(cl->resend_ev, &cl->resend_interval);
}


static void
make_client(int proposer_id, int value_size)
{
	struct client* c;
	c = malloc(sizeof(struct client));
	if(c == NULL){
		return;
	}
	cl = c;
	struct event_config *cfg = event_config_new();
  event_config_avoid_method(cfg, "epoll");
  c->base = event_base_new_with_config(cfg);
	c->id = rand();
	printf("id is %d\n",c->id );

  if(use_socket){
    if(cansend){
      c->bev = connect_to_server(c, dest_addr, dest_port);
      if (c->bev == NULL){
        printf("Could not start TCP connection\n");
        exit(1);
      }
    }else{
      c->s = server_new(c);
      if (c->s == NULL){
        printf("Could not start TCP connection\n");
        exit(1);
      }
      server_listen(c->s,dest_addr, dest_port);
    }
  }

	if(use_chardevice){
		open_file(c);
		size_t s = sizeof(struct client_value) + value_size;
    int opt = -1;
    if(trimvalue > 0)
      opt = TRIM;
		write_file(c->fd, &s, VALUE, sizeof(size_t), opt);
    cl->evread = event_new(cl->base, cl->fd, EV_READ | EV_PERSIST, on_read_file, event_self_cbarg());
    event_add(cl->evread, NULL);
	}

	c->value_size = value_size;

	c->sig = evsignal_new(c->base, SIGINT, handle_sigint, c->base);
	evsignal_add(c->sig, NULL);
	if(cansend){
		c->send_buffer = malloc(sizeof(struct client_value) + c->value_size);
		init_socket(c);

		// print statistic every 1 sec
		c->stats_interval = (struct timeval){1, 0};
		c->resend_interval = (struct timeval){1, 0};
		c->stats_ev = evtimer_new(c->base, on_stats, c);
		event_add(c->stats_ev, &c->stats_interval);

		// resend value after 1 sec I did not receive anything
		c->resend_ev = evtimer_new(c->base, on_resend, NULL);
		event_add(c->resend_ev, &c->resend_interval);

    if(!use_socket){
      for (size_t i = 0; i < outstanding; i++) {
        client_submit_value(cl);
      }
    }

	}

	event_base_dispatch(c->base);
	client_free(c, use_chardevice, cansend, use_socket);
	event_config_free(cfg);
}

static void
start_client(int proposer_id, int value_size)
{
	make_client(proposer_id, value_size);
	signal(SIGPIPE, SIG_IGN);
	libevent_global_shutdown();
}

int
main(int argc, char const *argv[])
{
	int i = 1;
	int proposer_id = 0;
	int value_size = 64;
	struct timeval seed;

	while (i != argc) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
			usage(argv[0]);
		else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--outstanding") == 0)
			outstanding = atoi(argv[++i]);
		else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--value-size") == 0)
			value_size = atoi(argv[++i]);
		else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--proposer-id") == 0)
			proposer_id = atoi(argv[++i]);
		else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--client") == 0)
			cansend = 1;
    else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--learner") == 0){
      isalearner = 1;
      trimvalue = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0 ){
			use_chardevice = 1;
			learner_id = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--socket") == 0 ){
			use_socket = 1;
			dest_addr = (char *) argv[++i];
      if(i+1 < argc)
        dest_port = atoi(argv[++i]);
      else{
        printf("Plaese add also the port!\n");
        i++;
      }
		} else
			usage(argv[0]);
		i++;
	}

  if(!((cansend && !isalearner) || (!cansend && isalearner))){
    printf("Either choose a client or a learner!\n");
    usage(argv[0]);
    exit(1);
  }


	if(cansend && !((use_chardevice && !use_socket) || (!use_chardevice && use_socket))){
		printf("As client, either use chardevice or connect remotely to a listener\n");
    usage(argv[0]);
    exit(1);
	}

	if(isalearner && !use_chardevice){
		printf("As learner, you must read from chardevice\n");
    usage(argv[0]);
    exit(1);
	}

	gettimeofday(&seed, NULL);
	srand(seed.tv_usec);
	start_client(proposer_id, value_size);

	return 0;
}
