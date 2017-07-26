obj-m += kproposer.o
obj-m += kclient.o
obj-m += kacceptor.o
obj-m += klearner.o

kacceptor-objs:= \
	kpaxos/kacceptor.o \
	kpaxos/kernel_udp.o
	# paxos/acceptor.o \
	# paxos/carray.o \
	# paxos/learner.o \
	# paxos/paxos.o \
	# paxos/proposer.o \
	# paxos/quorum.o \
	# paxos/storage_mem.o \
	# paxos/storage_utils.o \
	# paxos/storage.o \
	# evpaxos/message.o \
	# evpaxos/paxos_types_pack.o \
	# evpaxos/config.o \
	# evpaxos/peers.o \
	# evpaxos/evacceptor.o


klearner-objs:= \
	kpaxos/klearner.o \
	kpaxos/kernel_udp.o
	# paxos/acceptor.o \
	# paxos/carray.o \
	# paxos/learner.o \
	# paxos/paxos.o \
	# paxos/proposer.o \
	# paxos/quorum.o \
	# paxos/storage_mem.o \
	# paxos/storage_utils.o \
	# paxos/storage.o

kproposer-objs:= \
	kpaxos/kproposer.o \
	kpaxos/kernel_udp.o
# 	paxos/acceptor.o \
# 	paxos/carray.o \
# 	paxos/learner.o \
# 	paxos/paxos.o \
# 	paxos/proposer.o \
# 	paxos/quorum.o \
# 	paxos/storage_mem.o \
# 	paxos/storage_utils.o \
# 	paxos/storage.o

kclient-objs:= \
	kpaxos/kclient.o \
	kpaxos/kernel_udp.o
# 	paxos/acceptor.o \
# 	paxos/carray.o \
# 	paxos/learner.o \
# 	paxos/paxos.o \
# 	paxos/proposer.o \
# 	paxos/quorum.o \
# 	paxos/storage_mem.o \
# 	paxos/storage_utils.o \
# 	paxos/storage.o

EXTRA_CFLAGS:=  -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include

ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
