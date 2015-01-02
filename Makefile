##
## Copyright (C) 2014 SCALITY SA - http://www.scality.com
##
## This file is part of ScalityRestBlock.
##
## ScalityRestBlock is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## ScalityRestBlock is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with ScalityRestBlock.  If not, see <http://www.gnu.org/licenses/>.
##
##

TARGET := srb

srb-objs := srb_driver.o srb_sysfs.o srb_cdmi.o srb_http.o jsmn/jsmn.o
obj-m := $(TARGET).o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y += -g3 -O2 -Wall -Wextra -Wno-unused-parameter -Werror -Warray-bounds -D_REENTRANT -DJSMN_PARENT_LINKS

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
