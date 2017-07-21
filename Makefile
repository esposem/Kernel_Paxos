obj-m += kproposer.o
obj-m += kclient.o
obj-m += klearner.o
obj-m += kacceptor.o

kacceptor-objs:= \
	paxos/acceptor.o \
	paxos/carray.o \
	paxos/learner.o \
	paxos/paxos.o \
	paxos/proposer.o \
	paxos/quorum.o \
	paxos/storage_mem.o \
	paxos/storage_utils.o \
	paxos/storage.o


EXTRA_CFLAGS:= -I$(PWD)/paxos

ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
