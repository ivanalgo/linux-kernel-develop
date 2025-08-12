/* SPDX-License-Identifier: GPL-2.0 */

#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <linux/msharefs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <setjmp.h>

#include "../kselftest_harness.h"

#define MOUNT_POINT "/sys/fs/mshare"
#define FS_TYPE     "msharefs"

#define MSHARE_INFO MOUNT_POINT "/mshare_info"
#define MSHARE_TEST MOUNT_POINT "/mshareme"
#define STRING "I am Msharefs"

#define GB(x) ((x) << 30)

extern bool mount_sharefs(const char *target, const char *fstype);
extern void umount_sharefs(const char *target, const char *fstype);

FIXTURE(mshare)
{
	uint64_t align_size;
	int fd;
	bool mounted;
};

FIXTURE_SETUP(mshare)
{
	int fd;
	uint64_t size;
	char req[128];

	self->mounted = mount_sharefs(MOUNT_POINT, FS_TYPE);

	fd = open(MSHARE_INFO, O_RDONLY);
	ASSERT_NE(fd, -1);
	read(fd, req, sizeof(req));
	size = atoll(req);
	ASSERT_EQ(size, GB(512UL));
	close(fd);

	fd = open(MSHARE_TEST, O_RDWR|O_CREAT, 0600);
	ftruncate(fd, size);

	self->align_size = size;
	self->fd = fd;
}

FIXTURE_TEARDOWN(mshare)
{
	ASSERT_EQ(close(self->fd), 0);
	ASSERT_EQ(unlink(MSHARE_TEST), 0);
	if (!self->mounted)
		umount_sharefs(MOUNT_POINT, FS_TYPE);
}

static int mshare_ioctl_mapping(int fd, uint64_t size)
{
	struct mshare_create mcreate;

	mcreate.region_offset = 0;
	mcreate.size = size;
	mcreate.offset = 0;
	mcreate.prot = PROT_READ | PROT_WRITE;
	mcreate.flags = MAP_ANONYMOUS | MAP_SHARED | MAP_FIXED;
	mcreate.fd = -1;

	return ioctl(fd, MSHAREFS_CREATE_MAPPING, &mcreate);
}

static int mshare_ioctl_munmap(int fd, uint64_t size)
{
	struct mshare_unmap munmap;

	munmap.region_offset = 0;
	munmap.size = size;

	return ioctl(fd, MSHAREFS_UNMAP, &munmap);
}

TEST_F(mshare, ioctl)
{
	ASSERT_EQ(mshare_ioctl_mapping(self->fd, self->align_size), 0);
	ASSERT_EQ(mshare_ioctl_munmap(self->fd, self->align_size), 0);
}

TEST_F(mshare, process_mmap_munmap)
{
	void *addr;

	mshare_ioctl_mapping(self->fd, self->align_size);

	addr = mmap((void *)0, self->align_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED, self->fd, 0);
	ASSERT_NE(addr, MAP_FAILED);
	ASSERT_EQ(munmap(addr, self->align_size), 0);


	void *start = (void *)(self->align_size * 2);
	addr = mmap(start, self->align_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_FIXED, self->fd, 0);
	ASSERT_EQ(addr, start);
	ASSERT_EQ(munmap(addr, self->align_size), 0);

	mshare_ioctl_munmap(self->fd, self->align_size);
}

TEST_F(mshare, shared_mem)
{
	char *addr;
	void *start = 0;

	/* parent process to write data to shared memory */
	mshare_ioctl_mapping(self->fd, self->align_size);
	addr = mmap(start, self->align_size, PROT_READ | PROT_WRITE,
			MAP_SHARED, self->fd, 0);
	ASSERT_NE(addr, MAP_FAILED);
	memcpy(addr, STRING, sizeof(STRING));

	pid_t pid = fork();
	if (pid == 0) {
		/* child process to retrieve the data from shareed memory */
		start = (void *)(self->align_size * 3);
		addr = mmap(start, self->align_size, PROT_READ,
				MAP_SHARED, self->fd, 0);
		ASSERT_EQ(addr, start);
		ASSERT_EQ(memcmp(addr, STRING, sizeof(STRING)), 0);
		exit(0);
	} else {
		ASSERT_NE(waitpid(pid, NULL, 0), -1);
		munmap(addr, self->align_size);
		mshare_ioctl_munmap(self->fd, self->align_size);
	}
}

static sigjmp_buf jmp_env;
static volatile sig_atomic_t signal_received = 0;
static void handle_sigsegv(int sig) {
    signal_received = 1;
    siglongjmp(jmp_env, 1);
}

TEST_F(mshare, ioctl_unmap)
{
	char *addr;

	signal(SIGSEGV, handle_sigsegv);

	if (sigsetjmp(jmp_env, 1) == 0) {
		/* Reach here for the first time of executing sigsetjmp */
		mshare_ioctl_mapping(self->fd, self->align_size);
		addr = mmap(NULL, self->align_size, PROT_READ | PROT_WRITE,
				MAP_SHARED, self->fd, 0);
		ASSERT_NE(addr, MAP_FAILED);
		addr[0] = 'M';

		/* munmap vma for host mm */
		mshare_ioctl_munmap(self->fd, self->align_size);
		/*
		 * Will generate SIGSEGV signal as ioctl has already cleaned
		 * shared page table
		 */
		addr[0] = 'D';
	}

	/* Reach here from siglongjmp in signal handler */
	signal(SIGSEGV, SIG_DFL);
	ASSERT_EQ(signal_received, 1);
	/* munmap memory region */
	if (addr != MAP_FAILED)
		munmap(addr, self->align_size);
}

TEST_HARNESS_MAIN
