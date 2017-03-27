#KDIR:= ~/ESP/SDK/sysroots/i586-poky-linux/usr/src/kernel/
KDIR:=/home/esp/SDK/sysroots/i586-poky-linux/usr/src/kernel
#PWD:= $(shell pwd)

CC = i586-poky-linux-gcc
ARCH = x86
CROSS_COMPILE = i586-poky-linux-
SROOT= /home/esp/SDK/sysroots/i586-poky-linux/

APP1 = main_2

obj-m:= i2c_flash.o


all:
	make ARCH=x86 CROSS_COMPILE=i586-poky-linux- -C $(KDIR) M=$(PWD) modules
	i586-poky-linux-gcc -o $(APP1) main_2.c --sysroot=$(SROOT)  

clean:
	
	make ARCH=x86 CROSS_COMPILE=i586-poky-linux- -C $(KDIR) M=$(PWD) clean
	rm -f $(APP1)
	
