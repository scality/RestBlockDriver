TARGET := dewblock

dewblock-objs := dewb.o dewb_cdmi.o dewb_http.o
obj-m := $(TARGET).o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
