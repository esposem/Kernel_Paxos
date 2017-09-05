obj-m +=kclient0.o \
kacceptor0.o \
kacceptor1.o \
kacceptor2.o \
kproposer0.o \
klearner0.o \
klearner1.o \
klearner2.o \
klearner3.o \
klearner4.o \
kreplica0.o \
kreplica1.o \
kreplica2.o \
kclient1.o \
kclient2.o #\
kproposer1.o \
kproposer2.o

PAX_OBJ= kpaxos/kernel_udp.o \
paxos/carray.o \
paxos/paxos.o \
paxos/quorum.o \
paxos/storage_mem.o \
paxos/storage_utils.o \
paxos/storage.o \
evpaxos/message.o \
evpaxos/paxos_types_pack.o \
evpaxos/config.o \
evpaxos/peers.o

CL_OBJ= \
kpaxos/kclient.o \
evpaxos/evlearner.o \
paxos/learner.o \
$(PAX_OBJ)

PROP_OBJ= \
	kpaxos/kproposer.o \
	evpaxos/evproposer.o \
	paxos/proposer.o \
	$(PAX_OBJ)

ACC_OBJ= \
 	kpaxos/kacceptor.o \
	evpaxos/evacceptor.o \
	paxos/acceptor.o \
	$(PAX_OBJ)

LEARN_OBJ= \
	kpaxos/klearner.o \
	evpaxos/evlearner.o \
	paxos/learner.o \
	kpaxos/kernel_device.o \
	$(PAX_OBJ)

REP_OBJ= \
  kpaxos/kreplica.o \
	evpaxos/evlearner.o \
	evpaxos/evproposer.o \
	evpaxos/evacceptor.o \
	paxos/acceptor.o \
	paxos/learner.o \
	paxos/proposer.o \
	evpaxos/evreplica.o \
	$(PAX_OBJ)

EXTRA_CFLAGS:= -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include
ccflags-y:= -std=gnu99 -Wno-declaration-after-statement -O2

LFLAGS = -levent -I /usr/local/include -L /usr/local/lib
USR_OBJ= kpaxos/client_user.c paxos/include/paxos_types.h kpaxos/include/kernel_device.h

kclient0-y:= $(CL_OBJ)
kclient1-y:= $(CL_OBJ)
kclient2-y:= $(CL_OBJ)

kproposer0-y:= $(PROP_OBJ)
# kproposer1-y:= $(PROP_OBJ)
# kproposer2-y:= $(PROP_OBJ)

kacceptor0-y:= $(ACC_OBJ)
kacceptor1-y:= $(ACC_OBJ)
kacceptor2-y:= $(ACC_OBJ)

klearner0-y:= $(LEARN_OBJ)
klearner1-y:= $(LEARN_OBJ)
klearner2-y:= $(LEARN_OBJ)
klearner3-y:= $(LEARN_OBJ)
klearner4-y:= $(LEARN_OBJ)

kreplica0-y:= $(REP_OBJ)
kreplica1-y:= $(REP_OBJ)
kreplica2-y:= $(REP_OBJ)


all: client_user
	make -C  /lib/modules/$(shell uname -r)/build M=$(PWD) modules

client_user: $(USR_OBJ)
	$(CC) $(EXTRA_CFLAGS) -o $@0 $< $(LFLAGS)
	$(CC) $(EXTRA_CFLAGS) -o $@1 $< $(LFLAGS)
	$(CC) $(EXTRA_CFLAGS) -o $@2 $< $(LFLAGS)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm client_user*
