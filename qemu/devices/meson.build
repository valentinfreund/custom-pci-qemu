# Custom QEMU devices. This directory is symlinked into the QEMU tree as
# hw/custom by scripts/setup-qemu.sh. Add one line per new device.
system_ss.add(when: 'CONFIG_CUSTOM_PCI_DEVICE', if_true: files('custom-pci-device.c'))
