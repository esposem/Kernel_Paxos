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
kpaxos/stats.o \
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

KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(PWD)/build
BUILD_DIR_MAKEFILE ?= $(PWD)/build/Makefile

C_COMP:= -std=c99
G_COMP:= -std=gnu99
USR_FLAGS:= -Wall -D user_space
USR_SRCS := $(wildcard kpaxos/user_*.c)
USR_CL := $(filter-out kpaxos/user_learner.c, $(USR_SRCS))
USR_LEARN := $(filter-out kpaxos/user_client.c, $(USR_SRCS))
USRC_OBJS := $(patsubst kpaxos/%.c, $(BUILD_DIR)/%.o, $(USR_CL))
USRL_OBJS := $(patsubst kpaxos/%.c, $(BUILD_DIR)/%.o, $(USR_LEARN))

EXTRA_CFLAGS:= -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include -I$(HOME)/local/include
ccflags-y:= $(G_COMP) -Wall -Wno-declaration-after-statement -Wframe-larger-than=3100 -O3

LFLAGS = -levent -I /home/ubuntu/local/include -L /home/ubuntu/local/lib

all: $(BUILD_DIR) kernel_app user_client user_learner

kernel_app: $(BUILD_DIR_MAKEFILE)
	make -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) modules

$(BUILD_DIR):
	mkdir -p "$@/paxos"
	mkdir -p "$@/evpaxos"
	mkdir -p "$@/kpaxos"

$(BUILD_DIR_MAKEFILE): $(BUILD_DIR)
	touch "$@"

$(BUILD_DIR)/%.o: kpaxos/%.c
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -c $< -o $@

user_client: $(USRC_OBJS)
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -o $(BUILD_DIR)/$@ $^ $(LFLAGS)

user_learner: $(USRL_OBJS)
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -o $(BUILD_DIR)/$@ $^ $(LFLAGS)

###########################################################################
clean:
	make -C $(KDIR) M=$(BUILD_DIR) src=$(PWD) clean
	-rm -rf build
