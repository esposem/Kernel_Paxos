PAX_OBJ= \
paxos/carray.o \
paxos/paxos.o \
paxos/quorum.o \
paxos/storage_mem.o \
paxos/storage_utils.o \
paxos/storage.o \
evpaxos/message.o \
evpaxos/paxos_types_pack.o \
evpaxos/config.o \
evpaxos/peers.o \
evpaxos/eth.o \
kpaxos/kfile.o

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
	kpaxos/kernel_device.o \
  kpaxos/kreplica.o \
	evpaxos/evlearner.o \
	evpaxos/evproposer.o \
	evpaxos/evacceptor.o \
	paxos/acceptor.o \
	paxos/learner.o \
	paxos/proposer.o \
	evpaxos/evreplica.o \
	$(PAX_OBJ)

################# MODIFY HERE FOR MORE MODULES ##############
obj-m += \
kproposer.o \
klearner.o \
kacceptor.o \
kreplica.o \
kclient.o

kclient-y:= $(CL_OBJ)
kproposer-y:= $(PROP_OBJ)
kacceptor-y:= $(ACC_OBJ)
klearner-y:= $(LEARN_OBJ)
kreplica-y:= $(REP_OBJ)

##############################################################
C_COMP:= -std=c99
G_COMP:= -std=gnu99
SUPPRESSED_WARN:= -Wno-declaration-after-statement -Wframe-larger-than=1550

USR_FLAGS:= -Wall -D user_space
USR_OBJ:=user_app.o user_udp.o user_levent.o user_stats.o
_USR_HEAD:= user_levent.h user_stats.h user_udp.h kernel_client.h
USR_HEAD:= $(patsubst %,$(I_DIR)/include/%,$(_USR_HEAD))

LFLAGS = -levent -I /usr/local/include -L /usr/local/lib
EXTRA_CFLAGS:= -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include
ccflags-y:= $(G_COMP) -Wall $(SUPPRESSED_WARN) -O2

.PHONY: all clean

#user_app
all:
	make -C  /lib/modules/$(shell uname -r)/build M=$(PWD) modules

%.o: $(I_DIR)/%.c $(USR_HEAD)
	$(CC) $(USR_FLAGS) $(ccflags-y) -c $< -o $@

user_app: $(USR_OBJ)
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(LFLAGS)

# rm user_app*
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
