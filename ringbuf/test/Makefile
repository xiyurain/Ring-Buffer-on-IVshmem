ifneq ($(KERNELRELEASE),)
	obj-m := send_msg.o

else
	KERNELDIR ?= /home/popcorn/kernel_src/linux-5.15.1/
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

ubuntu:
	$(MAKE) -C /lib/modules/5.4.0-90-generic/build M=$(PWD) modules

clean:
	rm *.o *.ko *.mod *.mod.c *.order *.symvers > /dev/null
endif
