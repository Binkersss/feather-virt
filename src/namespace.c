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

// Build UID map content using current user's UID
int build_uid_map_content(char *buf, size_t buf_len)
{
    uid_t uid = getuid();
    return snprintf(buf, buf_len, "0 %d 1\n", uid);
}

// Build GID map content using current user's GID
int build_gid_map_content(char *buf, size_t buf_len)
{
    gid_t gid = getgid();
    return snprintf(buf, buf_len, "0 %d 1\n", gid);
}

// Build path to /proc/<pid>/<filename>
void build_proc_path(pid_t pid, const char *filename, char *buf,
		     size_t buf_len)
{
    snprintf(buf, buf_len, "/proc/%d/%s", pid, filename);
}

// Write content to a /proc file
int write_proc_file(pid_t pid, const char *filename, const char *content)
{
    char path[256];
    build_proc_path(pid, filename, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) {
	perror(path);
	return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return (written == len) ? 0 : -1;
}

void setup_uid_gid_map(pid_t child)
{
    char map[128];
    FILE *f;

    /* Setup UID mapping: container UID 0 -> host UID */
    build_uid_map_content(map, sizeof(map));
    write_proc_file(child, "uid_map", map);

    /* Deny setgroups */
    write_proc_file(child, "setgroups", "deny\n");

    /* Setup GID mapping: container GID 0 -> host GID */
    build_gid_map_content(map, sizeof(map));
    write_proc_file(child, "gid_map", map);
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
