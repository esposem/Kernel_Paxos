# Kernel_Paxos
Kernel Modules that implements Paxos protocol

Tested on Ubuntu 17.04 zesty, kernel 4.10.0-33-generic.

The logic implementation of Paxos protocol used in these modules has been taken from [libpaxos](http://libpaxos.sourceforge.net/)

## Description
### Kernel space
There are 5 kind of modules: Kacceptor, Kproposer, Klearner, Kclient and Kreplica.

<b>Kacceptor</b>: Assumes the role of an acceptor in Paxos protocol. An acceptor receive 1A (prepare) and 2A (accept request) from proposer, and sends back 1B to proposer (promise) and 2B (accepted) to learners and proposer.

<b>Kproposer</b>: Assumes the role of a proposer in Paxos protocol. A proposer send 1A (prepare) and 2A (accept request) to acceptor, and receive 1B (promise) 2B (accepted) from acceptor, and CV (client value) from client.

<b>Klearner</b>: Assumes the role of a learner in Paxos protocol.
Learner receives 2B (accepted) messages from acceptor and deliver to user space application.

<b>Kclient</b>: Assumes the role of a client and learner in Paxos protocol. The kclient send a message to a kproposer, and
in the same time make a learner start so it will receive the value it sent. It keeps track of the delay from the sent to the received moment to calculate statistics. The kclient can be substituted by the client application in user space

<b>Kreplica</b>: Assumes the role of an replica in Paxos protocol. A replica contains the logic of proposer, acceptor and learner.

### User space

There are 2 kind of user space applications: Client and Learner.

<b> Client</b>: Sends message to the proposer that is in kernel space. The client can either attach via chardavice to a Klearner, or receive the client value (to calculate statistics) via user space socket by the user space Learner application.

<b> Learner</b>: Attaches to a Klearner via chardevice. It sends received value to the client through user space sockets.

## Difference from libpaxos

There are some difference between Kernel_Paxos and Libpaxos: first of all, Libpaxos uses libevent, msgpacker and khash.
Obviously, these are user space libraries, that cannot be used in kernel programming. However, I still use libevent in the user space applications.

<b> libevent</b>: libevent handled asynchronous timeout handling and tcp connections. I had to implement them manually. I use UDP connection and the timeout is somehow calculated after the udp_server_receive blocking call (if nothing is received, block lasts for 100 ms, then unblock).

<b> msgpacker</b>: msgpacker is used to pack the message to send, so the message can be read by big or little endian platform. I had to do it manually too, manipulating the integers and checking the `_BIG_ENDIAN` and `_LITTLE_ENDIAN` flags inside the kernel.

<b> khash</b>: khash uses floating point internally, and the kernel does not like it. Therefore I used uthash, that is an hashmap that does not use floating point operations.

## How to run

You first need to compile the whole thing. Please refer to next section to select which module to compile. Once compiled, the command to load a kernel module is `sudo insmod module_name.ko parameters`. To unload it is `sudo rmmod module_name.ko`. `module_name` is the name of the module. The command to run an application is `./application`.

### Compiling
Since there are a lot of different configurations to make paxos protocol working (ex 3 replicas and one client, 1 proposer 3 acceptor 1 client, multiple clients, multiple learners, ecc... ), it might be necessary to modify the Makefile to add/remove modules.

To add/remove a module, you just need to modify the `obj-m` list of files. In addition, for each file added/removed, you must also modify the filename-y entry. For example, let's say a new acceptor is needed:<br>
Go to `obj-m` list and add to the last line the entry kacceptor3.ko<br>
`obj-m += \` <br>
`kclient0.o \` <br>
`kclient1.o \` <br>
`kclient2.o \` <br>
`kacceptor0.o \` <br>
`...` <br>
`kreplica2.o ` change this  to this `kreplica2.o kacceptor3.ko`<br>

Then, since we are adding a kacceptor, add it to the kacceprtor section<br>
`kacceptor0-y:= $(ACC_OBJ)`<br>
`kacceptor1-y:= $(ACC_OBJ)`<br>
`kacceptor2-y:= $(ACC_OBJ)`<br>
`kacceptor3-y:= $(ACC_OBJ)` add this line<br>

### Parameters

Each module and user application has its parameters. Parameters info of kernel modules can be seen by calling `modinfo module_name.ko`. Parameters info of applications can be seen by calling the application with `-h` flag. All parameters can be inserted in any order.

<b>Kacceptor</b>: `sudo insmod kacceptorX.ko id=X`. Only parameter here is the id of the acceptor.

<b>Kproposer</b>: `sudo insmod kproposerX.ko id=X`. Only parameter here is the id of the proposer.

<b>Klearner</b>: `sudo insmod klearnerX.ko id=X cantrim=X catch_up=X`. Here apart from the id there is the cantrim flag, that tells after how many iid the learner should call trim, and the catch_up flag, that makes the learner get all the values accepted by acceptors that is missing (from iid 1).

<b>Kclient</b>: `sudo insmod kclientX.ko id=X proposer_id=X outstanding=X value_size=X`. Here apart from the id there is the proposer_id flag, that tells which proposer should the client connect (reading from the config file), the outstanding value, that tells how many values should the client send each time, and the value_size that tells how big the message should be.

<b>Kreplica</b>: `sudo insmod kreplicaX.ko id=X`. Only parameter here is the id of the proposer.

<b>client_user</b>: `./client_user [-h] [-o] [-v] [-p] [-c] [-d] [-s]`.<br>
`-h` display help<br>
`-o number` set outstanding value<br>
`-v number` set value size<br>
`-p number` set proposer id, which proposer to send the values<br>
`-c` allow the application to send the client values<br>
`-d number` set the connection with the klearnerX module through chardevice (ex `-d 2` connects to `klearner2`)
`-s ip port` connects to the learner/client associated to that ip and port.


### Scripts

Since there are a lot of modules here, I created some scripts to automatically run them. Parameters in square brackets are optionals. Ther scripts are:

<b>run.sh</b>: <br>Usage `./run.sh module_name number [module_name number] [module_name number]`.<br>`module_name` is the name of module and `number` is the number of modules with that name you want. You must provide at least one module_name and one number. <br>Example: <br>`./run.sh kclient 3` Load 3 kclient modules, more specifically it will perform <br>`sudo insmod kclient0.ko id=0`<br>`sudo insmod kclient1.ko id=1` <br>`sudo insmod kclient2.ko id=2`. If a module is already loaded, it will be unloaded first.

<b>learn.sh</b>: <br>Usage `./learn.sh number_of_learner [id_of_trim_learner] [trim_iid]`. `number_of_learner` is the number of learner modules you want to load (same as `number` in `run.sh`), `id_of_trim_learner` is the id of the module (between the loaded ones) that will periodically call trim, and `trim_iid` is after how many iid the special learner should call trim.<br>
Example:<br>
`./learn.sh 3 0 2000` Load 3 klearner modules, where the first one (klearner0) can trim after 2000 received iid. More specifically <br>`sudo insmod klearner0.ko id=0 cantrim=2000`<br>`sudo insmod klearner1.ko id=1 cantrim=0` <br>`sudo insmod klearner2.ko id=2 cantrim=0`.

<b>prop_acc.sh</b>: <br>Usage `./prop_acc.sh`. Loads 1 kproposer (kproposer0) and 3 acceptors (kacceptor0, kacceptor1, and kacceptor2 ). It internally uses `run.sh`.

[... USER SPACE ...]

## Known bugs

Both configurations with 3 replicas and 3 acceptors with 1 proposer connected to 3 clients crashes after ~ 2 hours of uninterrupted work.

## TODO

Config file to be read by module (for now parameters are hardcoded)
