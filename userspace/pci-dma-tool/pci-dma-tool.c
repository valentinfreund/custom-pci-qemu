// SPDX-License-Identifier: GPL-2.0
/*
 * pci-dma-tool: exercises the ioctl and mmap interface of drv_pci.
 *
 *   pci-dma-tool info     /dev/drv_pci0      print buffer and device sizes
 *   pci-dma-tool loopback /dev/drv_pci0      zero-copy loopback via mmap + ioctl
 *   pci-dma-tool badargs  /dev/drv_pci0      invalid requests must be rejected
 *   pci-dma-tool bench    /dev/drv_pci0 [N]  read/write vs. mmap + ioctl
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <linux/drv_pci.h>

static struct drv_pci_info info;

static int dma(int fd, __u32 dir, __u32 size, __u32 buf_off, __u32 dev_off)
{
	struct drv_pci_dma req = {
		.direction = dir, .size = size, .buf_offset = buf_off, .dev_offset = dev_off,
	};
	return ioctl(fd, DRV_PCI_IOC_DMA, &req);
}

static unsigned char *map_buffer(int fd)
{
	void *p = mmap(NULL, info.buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (p == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	return p;
}

static void fill_random(unsigned char *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		p[i] = (unsigned char)rand();
}

static int cmd_info(void)
{
	printf("instance %u\nbuf_size %u\nmem_size %u\n",
	       info.instance, info.buf_size, info.mem_size);
	return 0;
}

static int cmd_loopback(int fd)
{
	unsigned char *buf = map_buffer(fd);
	unsigned char *ref = malloc(info.buf_size);
	int ok = 1;

	if (!buf || !ref)
		return 1;

	/* Whole buffer: mmap -> device -> mmap */
	fill_random(buf, info.buf_size);
	memcpy(ref, buf, info.buf_size);
	if (dma(fd, DRV_PCI_DMA_TO_DEVICE, info.buf_size, 0, 0) ||
	    (memset(buf, 0, info.buf_size), dma(fd, DRV_PCI_DMA_FROM_DEVICE, info.buf_size, 0, 0))) {
		perror("DRV_PCI_IOC_DMA");
		return 1;
	}
	if (memcmp(buf, ref, info.buf_size)) {
		fprintf(stderr, "whole buffer: data mismatch\n");
		ok = 0;
	}

	/* Different offsets in buffer and device memory */
	fill_random(buf + 100, 500);
	memcpy(ref, buf + 100, 500);
	if (dma(fd, DRV_PCI_DMA_TO_DEVICE, 500, 100, 3000) ||
	    dma(fd, DRV_PCI_DMA_FROM_DEVICE, 500, 2000, 3000)) {
		perror("DRV_PCI_IOC_DMA");
		return 1;
	}
	if (memcmp(buf + 2000, ref, 500)) {
		fprintf(stderr, "offsets: data mismatch\n");
		ok = 0;
	}

	munmap(buf, info.buf_size);
	free(ref);
	return ok ? 0 : 1;
}

/* Expect a request to fail with a specific errno. */
static int expect_err(const char *what, int ret, int want)
{
	if (ret == -1 && errno == want)
		return 0;
	fprintf(stderr, "%s: expected %s, got %s\n", what, strerror(want),
		ret == -1 ? strerror(errno) : "success");
	return 1;
}

static int cmd_badargs(int fd)
{
	__u32 n = info.buf_size;
	struct drv_pci_dma req = { .direction = DRV_PCI_DMA_TO_DEVICE, .size = 16 };
	int bad = 0;
	void *p;

	bad += expect_err("size 0", dma(fd, DRV_PCI_DMA_TO_DEVICE, 0, 0, 0), EINVAL);
	bad += expect_err("size > buffer", dma(fd, DRV_PCI_DMA_TO_DEVICE, n + 1, 0, 0), EINVAL);
	bad += expect_err("buffer overflow", dma(fd, DRV_PCI_DMA_TO_DEVICE, 16, n - 8, 0), EINVAL);
	bad += expect_err("device overflow", dma(fd, DRV_PCI_DMA_TO_DEVICE, 16, 0, info.mem_size - 8), EINVAL);
	bad += expect_err("offset wrap-around", dma(fd, DRV_PCI_DMA_TO_DEVICE, 16, 0xfffffff8, 0), EINVAL);
	bad += expect_err("unknown direction", dma(fd, 3, 16, 0, 0), EINVAL);
	req.flags = 1;
	bad += expect_err("flags != 0", ioctl(fd, DRV_PCI_IOC_DMA, &req), EINVAL);
	req.flags = 0;
	req.reserved = 1;
	bad += expect_err("reserved != 0", ioctl(fd, DRV_PCI_IOC_DMA, &req), EINVAL);
	bad += expect_err("unknown ioctl", ioctl(fd, _IO(DRV_PCI_IOC_MAGIC, 0x7f)), ENOTTY);
	bad += expect_err("bad user pointer", ioctl(fd, DRV_PCI_IOC_DMA, (void *)16), EFAULT);

	p = mmap(NULL, n * 2, PROT_READ, MAP_SHARED, fd, 0);
	bad += expect_err("mmap larger than buffer", p == MAP_FAILED ? -1 : 0, EINVAL);
	p = mmap(NULL, n, PROT_READ, MAP_SHARED, fd, n);
	bad += expect_err("mmap with offset", p == MAP_FAILED ? -1 : 0, EINVAL);

	return bad ? 1 : 0;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int cmd_bench(int fd, int iterations)
{
	unsigned char *buf = map_buffer(fd);
	unsigned char *user = malloc(info.buf_size);
	double t, mb = 2.0 * iterations * info.buf_size / 1e6;

	if (!buf || !user)
		return 1;
	fill_random(user, info.buf_size);

	/* read()/write(): data is copied between user and kernel buffer */
	t = now();
	for (int i = 0; i < iterations; i++) {
		if (pwrite(fd, user, info.buf_size, 0) != info.buf_size ||
		    pread(fd, user, info.buf_size, 0) != info.buf_size) {
			perror("pread/pwrite");
			return 1;
		}
	}
	t = now() - t;
	printf("read/write:   %6.1f MB/s  (%5.1f us per transfer)\n", mb / t, t * 1e6 / (2 * iterations));

	/* mmap + ioctl: the process works directly on the DMA buffer */
	t = now();
	for (int i = 0; i < iterations; i++) {
		if (dma(fd, DRV_PCI_DMA_TO_DEVICE, info.buf_size, 0, 0) ||
		    dma(fd, DRV_PCI_DMA_FROM_DEVICE, info.buf_size, 0, 0)) {
			perror("DRV_PCI_IOC_DMA");
			return 1;
		}
	}
	t = now() - t;
	printf("mmap + ioctl: %6.1f MB/s  (%5.1f us per transfer)\n", mb / t, t * 1e6 / (2 * iterations));

	munmap(buf, info.buf_size);
	free(user);
	return 0;
}

int main(int argc, char **argv)
{
	int fd, ret;

	if (argc < 3) {
		fprintf(stderr, "usage: %s info|loopback|badargs|bench <device> [iterations]\n", argv[0]);
		return 2;
	}
	fd = open(argv[2], O_RDWR);
	if (fd < 0) {
		perror(argv[2]);
		return 1;
	}
	if (ioctl(fd, DRV_PCI_IOC_INFO, &info)) {
		perror("DRV_PCI_IOC_INFO");
		return 1;
	}
	srand(time(NULL));

	if (!strcmp(argv[1], "info"))
		ret = cmd_info();
	else if (!strcmp(argv[1], "loopback"))
		ret = cmd_loopback(fd);
	else if (!strcmp(argv[1], "badargs"))
		ret = cmd_badargs(fd);
	else if (!strcmp(argv[1], "bench"))
		ret = cmd_bench(fd, argc > 3 ? atoi(argv[3]) : 10000);
	else {
		fprintf(stderr, "unknown command %s\n", argv[1]);
		ret = 2;
	}
	close(fd);
	return ret;
}
