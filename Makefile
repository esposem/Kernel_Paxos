obj-m += proposer.o
obj-m += client.o
obj-m += learner.o
obj-m += acceptor.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
