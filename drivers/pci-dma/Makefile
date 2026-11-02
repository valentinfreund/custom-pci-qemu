obj-m += drv_pci.o

# Shared headers (register map, UAPI) from include/ of this repository.
# Buildroot copies this directory away from the repository and passes
# DRIVERLAB_INCLUDE explicitly (see buildroot-external/package/drv-pci).
DRIVERLAB_INCLUDE ?= $(src)/../../include
ccflags-y += -I$(DRIVERLAB_INCLUDE)

# Builds against the running host kernel by default.
# Buildroot passes its own kernel tree instead.
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
