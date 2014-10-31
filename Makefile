TARGET := srb

srb-objs := srb_driver.o srb_sysfs.o srb_cdmi.o srb_http.o jsmn/jsmn.o
obj-m := $(TARGET).o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y += -g3 -Wunused -Wuninitialized -O2 -Wall -Werror -Warray-bounds -D_REENTRANT -DJSMN_PARENT_LINKS

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
