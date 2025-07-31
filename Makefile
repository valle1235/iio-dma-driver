obj-m := fft_iio_dma.o

KDIR := ../linux
PWD := $(shell pwd)

all:
	make -C $(KDIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
