obj-m += drv_pci.o

# Builds against the running host kernel by default.
# Buildroot passes its own kernel tree instead (see buildroot-external/).
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
