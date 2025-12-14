# Custom PCI Device Emulation & Linux Driver

This project demonstrates how to emulate a custom PCI device using QEMU and includes a corresponding Linux kernel driver to interact with the virtual hardware. Notice: My Host OS is Ubuntu 20.04.

## Table of Contents
1. [Prerequisites](#1-prerequisites)
2. [Create Custom PCI Device in QEMU](#2-create-custom-pci-device-in-qemu)
3. [Create PCI Device Driver](#3-create-pci-device-driver)

---

## 1. Prerequisites

### System specification
* QEMU: v9.0.0
* Linux Kernel: Version 5.4

### Installation
Install the dependencies using your package manager:

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
└── logs/
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

