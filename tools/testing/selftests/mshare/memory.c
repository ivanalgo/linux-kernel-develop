// SPDX-License-Identifier: GPL-2.0

#include <linux/mman.h>

#include "../kselftest_harness.h"
#include "util.c"

FIXTURE(memory)
{
	char filename[128];
	int fd;

	char cgroup[128];

	void *addr;
	size_t align_size;
};

FIXTURE_SETUP(memory)
{
	ASSERT_NE(create_mshare_test_cgroup(self->cgroup, sizeof(self->cgroup)), -1);

	attach_to_cgroup(self->cgroup);

	self->align_size = mshare_get_info();
	self->fd = create_mshare_file(self->filename, sizeof(self->filename));
	ASSERT_NE(self->fd, -1);
	ASSERT_NE(ftruncate(self->fd, self->align_size), -1);

	ASSERT_NE(mshare_ioctl_mapping(self->fd, MB(2),
				MAP_ANONYMOUS | MAP_SHARED | MAP_FIXED),
			-1);
	self->addr = mmap(NULL, self->align_size, PROT_READ | PROT_WRITE,
						MAP_SHARED, self->fd, 0);
	ASSERT_NE(self->addr, MAP_FAILED);
}

FIXTURE_TEARDOWN(memory)
{
	ASSERT_NE(munmap(self->addr, self->align_size), -1);
	close(self->fd);

	ASSERT_NE(unlink(self->filename), -1);
	dettach_from_cgroup(self->cgroup);

	ASSERT_NE(remove_cgroup(self->cgroup), -1);
}

TEST_F(memory, swap)
{
	size_t swap_size;

	/* fill physical memory */
	memset(self->addr, 0x01, MB(2));

	/* force to reclaim the memory of mshare */
	ASSERT_NE(madvise(self->addr, MB(2), MADV_PAGEOUT), -1);

	swap_size = read_swap_from_cgroup(self->cgroup);
	ASSERT_NE(swap_size, -1);

	/* convert to bytes */
	swap_size *= 4096;

	ksft_print_msg("Tip: Please configure swap space before running this test.\n");

	/* allow an error of 10% */
	ASSERT_GT(swap_size, MB(2) * 9 / 10);
}

TEST_HARNESS_MAIN
