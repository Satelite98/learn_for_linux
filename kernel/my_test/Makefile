KERNEL_DIR = ~/wxwang/kernel/linux-2.6.22.6
PWD = $(shell pwd)
all:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) modules clean
	rm -rf modules.order

obj-m += myled.o