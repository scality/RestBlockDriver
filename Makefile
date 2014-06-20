TARGET := dewblock

dewblock-objs := dewb.o dewb_sysfs.o dewb_cdmi.o dewb_http.o jsmn/jsmn.o
obj-m := $(TARGET).o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y += -DJSMN_PARENT_LINKS

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
