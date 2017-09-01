# 1 client, 3 proposer 3 acceptor 2 learner

# PAX_OBJ=

obj-m += kclient0.o

kclient0-objs:= \
  kpaxos/kclient.o \
	evpaxos/evlearner.o \
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
	evpaxos/peers.o #\
 	evpaxos/evproposer.o \
	evpaxos/evacceptor.o

obj-m += kclient1.o

kclient1-objs:= \
  kpaxos/kclient.o \
	evpaxos/evlearner.o \
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
	evpaxos/peers.o #\
 	evpaxos/evproposer.o \
	evpaxos/evacceptor.o

obj-m += kclient2.o

kclient2-objs:= \
  kpaxos/kclient.o \
	evpaxos/evlearner.o \
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
	evpaxos/peers.o #\
 	evpaxos/evproposer.o \
	evpaxos/evacceptor.o

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
obj-m += klearner2.o
obj-m += klearner3.o
obj-m += klearner4.o
obj-m += klearner5.o
obj-m += klearner6.o
obj-m += klearner7.o
obj-m += klearner8.o

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

klearner2-objs:= \
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

klearner3-objs:= \
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

klearner4-objs:= \
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

klearner5-objs:= \
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

klearner6-objs:= \
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
klearner7-objs:= \
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

klearner8-objs:= \
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

# obj-m += kreplica0.o
# obj-m += kreplica1.o
# obj-m += kreplica2.o
#
# kreplica0-objs:= \
#   kpaxos/kreplica.o \
# 	evpaxos/evlearner.o \
# 	kpaxos/kernel_device.o \
# 	evpaxos/evproposer.o \
# 	evpaxos/evacceptor.o \
# 	evpaxos/evreplica.o \
# 	kpaxos/kernel_udp.o \
# 	paxos/acceptor.o \
# 	paxos/carray.o \
# 	paxos/learner.o \
# 	paxos/paxos.o \
# 	paxos/proposer.o \
# 	paxos/quorum.o \
# 	paxos/storage_mem.o \
# 	paxos/storage_utils.o \
# 	paxos/storage.o \
# 	evpaxos/message.o \
# 	evpaxos/paxos_types_pack.o \
# 	evpaxos/config.o \
# 	evpaxos/peers.o
#
# kreplica1-objs:= \
#   kpaxos/kreplica.o \
# 	evpaxos/evlearner.o \
# 	evpaxos/evproposer.o \
# 	evpaxos/evacceptor.o \
# 	kpaxos/kernel_udp.o \
# 	kpaxos/kernel_device.o \
# 	paxos/acceptor.o \
# 	paxos/carray.o \
# 	paxos/learner.o \
# 	paxos/paxos.o \
# 	paxos/proposer.o \
# 	paxos/quorum.o \
# 	paxos/storage_mem.o \
# 	paxos/storage_utils.o \
# 	paxos/storage.o \
# 	evpaxos/message.o \
# 	evpaxos/paxos_types_pack.o \
# 	evpaxos/config.o \
# 	evpaxos/peers.o \
# 	evpaxos/evreplica.o
#
# kreplica2-objs:= \
#   kpaxos/kreplica.o \
# 	evpaxos/evlearner.o \
# 	evpaxos/evproposer.o \
# 	evpaxos/evacceptor.o \
# 	kpaxos/kernel_udp.o \
# 	kpaxos/kernel_device.o \
# 	paxos/acceptor.o \
# 	paxos/carray.o \
# 	paxos/learner.o \
# 	paxos/paxos.o \
# 	paxos/proposer.o \
# 	paxos/quorum.o \
# 	paxos/storage_mem.o \
# 	paxos/storage_utils.o \
# 	paxos/storage.o \
# 	evpaxos/message.o \
# 	evpaxos/paxos_types_pack.o \
# 	evpaxos/config.o \
# 	evpaxos/peers.o \
# 	evpaxos/evreplica.o

EXTRA_CFLAGS:= -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include
ccflags-y:= -std=gnu99 -Wno-declaration-after-statement -O2

# $(PWD)/paxos/include/paxos_types.h

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	$(CC) -o client_user0 kpaxos/client_user.c -levent -I /usr/local/include -L /usr/local/lib
	$(CC) -o client_user1 kpaxos/client_user.c -levent -I /usr/local/include -L /usr/local/lib
	$(CC) -o client_user2 kpaxos/client_user.c -levent -I /usr/local/include -L /usr/local/lib

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm client_user0 client_user1 client_user2
