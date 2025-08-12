/* SPDX-License-Identifier: GPL-2.0 */

#include <stdio.h>
#include <stdbool.h>
#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>

#define MOUNT_POINT "/sys/fs/mshare"
#define FS_TYPE     "msharefs"

bool is_mounted(const char *target, const char *fstype) {
	FILE *fp;
	struct mntent *ent;
	bool found = false;

	fp = setmntent("/proc/mounts", "r");
	if (!fp) {
		perror("setmntent");
		exit(1);
	}

	while ((ent = getmntent(fp)) != NULL) {
		if (strcmp(ent->mnt_dir, target) == 0 &&
		    strcmp(ent->mnt_type, fstype) == 0) {
			found = true;
			break;
		}
	}

	endmntent(fp);
	return found;
}

bool mount_sharefs(const char *target, const char *fstype) {
	if (is_mounted(target, fstype))
		return true;

	if (mount(fstype, target, fstype, 0, NULL) != 0) {
		perror("mount");
		exit(1);
	}

	return false;
}

void umount_sharefs(const char *target, const char *fstype) {
	if (umount(target) != 0) {
		perror("umount");
		exit(1);
	}
}
