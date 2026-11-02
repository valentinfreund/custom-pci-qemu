# Builds with the host compiler by default; Buildroot passes CC and CFLAGS.
DRIVERLAB_INCLUDE ?= ../../include
CFLAGS ?= -O2
override CFLAGS += -Wall -Wextra -I$(DRIVERLAB_INCLUDE)/uapi

pci-dma-tool: pci-dma-tool.c $(DRIVERLAB_INCLUDE)/uapi/linux/drv_pci.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f pci-dma-tool
