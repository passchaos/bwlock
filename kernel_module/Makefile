# CFLAGS += -mno-sse -mno-sse2 -mno-mmx
# CFLAGS += -msoft-float -msse -msse2 -msse3 -msse4
obj-m += bwlock.o
bwlock-objs := bwlockmod.o utils.o perf.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

PWD := $(shell pwd)

all:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
