# Custom PCI Device Emulation & Linux Driver

This project demonstrates how to emulate a custom PCI device using QEMU and includes a corresponding Linux kernel driver to interact with the virtual hardware. Notice: My Host OS is Ubuntu 20.04.

**Workflow:  Setup QEMU  ->  Add custom PCI Device  ->  Build QEMU  ->  Start QEMU  ->  Add, Make & Load Driver**

## Table of Contents
1. [Prerequisites](#1-prerequisites)
2. [Create Custom PCI Device in QEMU](#2-create-custom-pci-device-in-qemu)
3. [Create PCI Device Driver](#3-create-pci-device-driver)

---

## 1. Prerequisites

### System specification
* QEMU: v9.0.0
* Linux Kernel: Version 5.4

### Install the dependencies

```bash
# Debian/Ubuntu systems
sudo apt update
sudo apt install \
  qemu-system-x86 \
  qemu-utils \
  virt-manager \
  libvirt-daemon-system \
  libvirt-clients \
  bridge-utils \
  build-essential \
  git
sudo apt install libsdl2-dev libgtk-3-dev libepoxy-dev libgbm-dev
sudo apt install libslirp-dev
```

Download an Ubuntu iso. i.e. **ubuntu-24.04.3-desktop-amd64.iso**

## 2. Create Custom PCI Device in QEMU

### Set up project
Our project structure should look like this. 

```text
qemu-pci-dev/
├── images/      # VM-Disks
├── isos/        # Linux-ISOs
├── build/       # QEMU-Builds / Device-Code
├── logs/        # I do not use logs
└── launchVM.sh  # launch script
```
```bash
mkdir -p ~/qemu-pci-dev/{images,isos,build,logs,filex}
cd ~/qemu-pci-dev
```

**Clone the QEMU github**
```bash
git clone https://gitlab.com/qemu-project/qemu.git
cd qemu
git checkout v9.0.0
```

**Copy your ISO to /isos**
```bash
cp ~/path/to/iso/ubuntu-24.04.3-desktop-amd64.iso ~/qemu-pci-dev/isos/ubuntu-24.04.3-desktop-amd64.iso
```

**Create your image in /images**
```bash
cd ~/qemu-pci-dev/images
qemu-img create -f qcow2 linux-pci-dev.qcow2 20G
#allocates 20 GB
```

### Customize the PCI device
Add the file containing the device to this location
```bash
cd ~/qemu-pci-dev/qemu/hw/misc
```
Manipulate the meson.build file in this directory 
```bash
vim meson.build
```
by adding this line
```python3
system_ss.add(when: 'CONFIG_PCI', if_true: files('custom-pci-device.c'))
```

### Build QEMU
this might take a moment
```bash
cd ~/qemu-pci-dev/build
../qemu/configure --target-list=x86_64-softmmu --enable-debug --enable-sdl --enable-gtk --enable-slirp 
make -j$(nproc)
```

### Use a bash script to launch QEMU
```bash
chmod +x launch-vm.sh
./launch-vm.sh
```

## 3. Create PCI Device Driver

**Installation of required packages in VM**
```bash
sudo apt update
sudo apt install \
  build-essential \
  linux-headers-$(uname -r) \
  gdb \
  make \
  git
```

**Mount the fileshare**
To share files between Host(files/) and the VM(filexhost)
```bash
mkdir filexhost
cd filexhost
sudo mount -t 9p -o trans=virtio,version=9p2000.L hostshare ~/filexhost
```

**Check if PCI device can be found**
```bash
lspci -nn
```

### Make & Load the Kernel Module

Currently, the driver displays a modest template.
It probes and maps one BAR.

drv_pci must be in the same directory as the Makefile when calling
```bash
make
```

Load the LKM with
```bash
sudo insmod drv_pci.ko
```

Inspect the kernel messages with
```bash
sudo dmesg | tail
```

Remove the kernel module
```bash
sudo rmmod drv_pci
```
