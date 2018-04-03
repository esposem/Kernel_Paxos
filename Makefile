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

########################################### MODIFY HERE ###########
obj-m += \
kproposer.o \
klearner.o \
kacceptor.o \
kreplica.o \
kclient.o

kclient-y:= $(CL_OBJ)
# kclient1-y:= $(CL_OBJ)
# kclient2-y:= $(CL_OBJ)

kproposer-y:= $(PROP_OBJ)
# kproposer1-y:= $(PROP_OBJ)
# kproposer2-y:= $(PROP_OBJ)

kacceptor-y:= $(ACC_OBJ)
# kacceptor1-y:= $(ACC_OBJ)
# kacceptor2-y:= $(ACC_OBJ)
#
klearner-y:= $(LEARN_OBJ)
# klearner1-y:= $(LEARN_OBJ)
# klearner2-y:= $(LEARN_OBJ)
# klearner3-y:= $(LEARN_OBJ)
# klearner4-y:= $(LEARN_OBJ)
#
kreplica-y:= $(REP_OBJ)
# kreplica1-y:= $(REP_OBJ)
# kreplica2-y:= $(REP_OBJ)
##############################################################
C_COMP:= -std=c99
G_COMP:= -std=gnu99
USR_FLAGS:= -Wall -D user_space
USR_OBJ:=user_app.o user_udp.o user_levent.o user_stats.o

EXTRA_CFLAGS:= -I$(PWD)/kpaxos/include -I$(PWD)/paxos/include -I$(PWD)/evpaxos/include
ccflags-y:= $(G_COMP) -Wall -Wno-declaration-after-statement -O2

LFLAGS = -levent -I /usr/local/include -L /usr/local/lib

# user_app
all:
	make -C  /lib/modules/$(shell uname -r)/build M=$(PWD) modules

user_udp.o: kpaxos/user_udp.c
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -c $< -o $@

user_levent.o: kpaxos/user_levent.c
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -c $< -o $@

user_stats.o: kpaxos/user_stats.c
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -c $< -o $@

user_app.o: kpaxos/user_app.c
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -c $< -o $@

############## MODIFY HERE IF YOU WANT TO ADD MORE USER SPACE APPLICATIONS
user_app: $(USR_OBJ)
	$(CC) $(USR_FLAGS) $(EXTRA_CFLAGS) -o $@ $^ $(LFLAGS)
###########################################################################

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm user_app*
