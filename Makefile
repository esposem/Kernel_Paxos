# 1 client, 3 proposer 3 acceptor 2 learner

obj-m += kclient0.o

kclient0-objs:= \
  kpaxos/kclient.o \
	evpaxos/evlearner.o \
	evpaxos/evproposer.o \
	evpaxos/evacceptor.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o



obj-m += kproposer0.o
obj-m += kproposer1.o
obj-m += kproposer2.o

kproposer0-objs:= \
	kpaxos/kproposer.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evproposer.o

kproposer1-objs:= \
	kpaxos/kproposer.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evproposer.o

kproposer2-objs:= \
	kpaxos/kproposer.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evproposer.o



obj-m += kacceptor0.o
obj-m += kacceptor1.o
obj-m += kacceptor2.o

kacceptor0-objs:= \
 	kpaxos/kacceptor.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evacceptor.o

kacceptor1-objs:= \
 	kpaxos/kacceptor.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evacceptor.o

kacceptor2-objs:= \
 	kpaxos/kacceptor.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evacceptor.o



obj-m += klearner0.o
obj-m += klearner1.o

klearner0-objs:= \
	kpaxos/klearner.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evlearner.o

klearner1-objs:= \
	kpaxos/klearner.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evlearner.o



obj-m += kreplica0.o
obj-m += kreplica1.o
obj-m += kreplica2.o

kreplica0-objs:= \
  kpaxos/kreplica.o \
	evpaxos/evlearner.o \
	evpaxos/evproposer.o \
	evpaxos/evacceptor.o \
	evpaxos/evreplica.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o

kreplica1-objs:= \
  kpaxos/kreplica.o \
	evpaxos/evlearner.o \
	evpaxos/evproposer.o \
	evpaxos/evacceptor.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evreplica.o

kreplica2-objs:= \
  kpaxos/kreplica.o \
	evpaxos/evlearner.o \
	evpaxos/evproposer.o \
	evpaxos/evacceptor.o \
	kpaxos/kernel_udp.o \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o \
	evpaxos/message.o \
	evpaxos/paxos_types_pack.o \
	evpaxos/config.o \
	evpaxos/peers.o \
	evpaxos/evreplica.o

EXTRA_CFLAGS:= -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include

ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
