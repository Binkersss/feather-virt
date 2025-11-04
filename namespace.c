#define _GNU_SOURCE
#include "namespace.h"
#include "overlay.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

void setup_uid_gid_map(pid_t child)
{
    char map_file[256];
    char map[128];
    FILE *f;

    /* Setup UID mapping: container UID 0 -> host UID */
    snprintf(map_file, sizeof(map_file), "/proc/%d/uid_map", child);
    f = fopen(map_file, "w");
    if (f) {
	snprintf(map, sizeof(map), "0 %d 1\n", getuid());
	fwrite(map, 1, strlen(map), f);
	fclose(f);
    } else {
	perror("open uid_map");
    }

    /* Deny setgroups */
    snprintf(map_file, sizeof(map_file), "/proc/%d/setgroups", child);
    f = fopen(map_file, "w");
    if (f) {
	fwrite("deny\n", 1, 5, f);
	fclose(f);
    }

    /* Setup GID mapping: container GID 0 -> host GID */
    snprintf(map_file, sizeof(map_file), "/proc/%d/gid_map", child);
    f = fopen(map_file, "w");
    if (f) {
	snprintf(map, sizeof(map), "0 %d 1\n", getgid());
	fwrite(map, 1, strlen(map), f);
	fclose(f);
    } else {
	perror("open gid_map");
    }
}

void setup_minimal_dev(void)
{
    mkdir_p("/dev", 0755);

    /* Create basic device nodes */
    if (mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3))
	&& errno != EEXIST)
	perror("mknod /dev/null");
    if (mknod("/dev/zero", S_IFCHR | 0666, makedev(1, 5))
	&& errno != EEXIST)
	perror("mknod /dev/zero");

    /* Setup devpts for pseudo-terminals */
    mkdir_p("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", 0, NULL) != 0)
	perror("mount devpts");
    if (mknod("/dev/ptmx", S_IFCHR | 0666, makedev(5, 2))
	&& errno != EEXIST)
	perror("mknod /dev/ptmx");
}
