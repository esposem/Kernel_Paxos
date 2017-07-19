obj-m += kproposer.o
obj-m += kclient.o
obj-m += klearner.o
obj-m += kacceptor.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
