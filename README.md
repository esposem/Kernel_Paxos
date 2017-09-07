# Kernel_Paxos
Kernel Modules that implements Paxos protocol

Tested on Ubuntu 17.04 zesty, kernel 4.10.0-33-generic.

The logic implementation of Paxos protocol used in these modules has been taken from [libpaxos](http://libpaxos.sourceforge.net/)

## Description
### Kernel space
There are 5 kind of modules: Kacceptor, Kproposer, Klearner, Kclient and Kreplica.

<b> Kacceptor</b>: Assumes the role of an acceptor in Paxos protocol. An acceptor receive 1A (prepare) and 2A (accept request) from proposer, and sends back 1B to proposer (promise) and 2B (accepted) to learners and proposer.

<b> Kproposer</b>: Assumes the role of a proposer in Paxos protocol. A proposer send 1A (prepare) and 2A (accept request) to acceptor, and receive 1B (promise) 2B (accepted) from acceptor, and CV (client value) from client.

<b> Klearner</b>: Assumes the role of a learner in Paxos protocol.
Learner receives 2B (accepted) messages from acceptor and deliver to user space application.

<b> Kclient</b>: Assumes the role of a client and learner in Paxos protocol. The kclient send a message to a kproposer, and
in the same time make a learner start so it will receive the value it sent. It keeps track of the delay from the sent to the received moment to calculate statistics. The kclient can be substituted by the client application in user space

<b> Kreplica</b>: Assumes the role of an replica in Paxos protocol. A replica contains the logic of proposer, acceptor and learner.

### User space

There are 2 kind of user space applications: Client and Learner.

<b> Client</b>: Sends message to the proposer that is in kernel space. The client can either attach via chardavice to a Klearner, or receive the client value (to calculate statistics) via user space socket by the user space Learner application.

<b> Learner</b>: Attaches to a Klearner via chardevice. It sends received value to the client through user space sockets.

## Difference from libpaxos

There are some difference between Kernel_Paxos and Libpaxos: first of all, Libpaxos uses libevent, msgpacker and khash.
Obviously, these are user space libraries, that cannot be used in kernel programming. However, I still use libevent in the user space applications.

<b> libevent</b>: libevent handled asynchronous timeout handling and tcp connections. I had to implement them manually. I use UDP connection and the timeout is somehow calculated after the udp_server_receive blocking call (if nothing is received, block lasts for 100 ms, then unblock).

<b> msgpacker</b>: msgpacker is used to pack the message to send, so the message can be read by big or little endian platform. I had to do it manually too, manipulating the integers and checking the BIG_ENDIAN and LITTLE_ENDIAN flags inside the kernel.

<b> khash</b>: khash uses floating point internally, and the kernel does not like it. Therefore I used uthash, that is an hashmap that does not use floating point operations.

## How to run

## Known bugs

## TODO
