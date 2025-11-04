#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define STACK_SIZE (1024*1024)
static char child_stack[STACK_SIZE];

/* Configurable paths */
const char *base_root = "/var/sandbox/basefs/alpine-3.20.2";	/* lowerdir (prebuilt base) */
const char *container_base = "/var/sandbox/containers";	/* per-container dirs */
const char *cgroup_base = "/sys/fs/cgroup/sandbox1";	/* simple cgroup path */

/* sync pipe: parent writes merged path; child reads it */
static int sync_pipe[2] = { -1, -1 };

/* Helper to create directories recursively */
static void mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    size_t len;
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);
    if (len == 0)
	return;
    if (tmp[len - 1] == '/')
	tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
	if (*p == '/') {
	    *p = '\0';
	    mkdir(tmp, mode);
	    *p = '/';
	}
    }
    mkdir(tmp, mode);
}

/* Create minimal /dev inside sandbox (inside the chroot) */
static void setup_minimal_dev()
{
    mkdir_p("/dev", 0755);
    /* mknod requires privileges; this usually works if running as root or in a user ns with caps */
    if (mknod("/dev/null", S_IFCHR | 0666, makedev(1, 3))
	&& errno != EEXIST)
	perror("mknod /dev/null");
    if (mknod("/dev/zero", S_IFCHR | 0666, makedev(1, 5))
	&& errno != EEXIST)
	perror("mknod /dev/zero");

    mkdir_p("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", 0, NULL) != 0)
	perror("mount devpts");
    if (mknod("/dev/ptmx", S_IFCHR | 0666, makedev(5, 2))
	&& errno != EEXIST)
	perror("mknod /dev/ptmx");
}

/* Setup cgroup v2 for the child PID (simple, writes limits into a single cgroup) */
static void setup_cgroup(pid_t child)
{
    if (mkdir(cgroup_base, 0755) && errno != EEXIST)
	perror("mkdir cgroup failed");

    char path[256];

    snprintf(path, sizeof(path), "%s/memory.max", cgroup_base);
    FILE *f = fopen(path, "w");
    if (f) {
	fprintf(f, "%llu", (unsigned long long) 128 * 1024 * 1024);
	fclose(f);
    }

    snprintf(path, sizeof(path), "%s/cpu.max", cgroup_base);
    f = fopen(path, "w");
    if (f) {
	fprintf(f, "50000 100000");
	fclose(f);
    }

    snprintf(path, sizeof(path), "%s/pids.max", cgroup_base);
    f = fopen(path, "w");
    if (f) {
	fprintf(f, "10");
	fclose(f);
    }

    snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_base);
    f = fopen(path, "w");
    if (f) {
	fprintf(f, "%d", child);
	fclose(f);
    }
}

/* Prepare overlayfs mount for given child pid. On success, merged_out contains merged path. */
static int
setup_overlay_root_for_pid(pid_t pid, char *merged_out, size_t merged_len)
{
    char upper[512], work[512], merged[512], container_dir[512];

    snprintf(container_dir, sizeof(container_dir), "%s/%d", container_base,
	     (int) pid);
    snprintf(upper, sizeof(upper), "%s/upper", container_dir);
    snprintf(work, sizeof(work), "%s/work", container_dir);
    snprintf(merged, sizeof(merged), "%s/rootfs", container_dir);

    mkdir_p(container_base, 0755);
    mkdir_p(container_dir, 0755);
    mkdir_p(upper, 0755);
    mkdir_p(work, 0755);
    mkdir_p(merged, 0755);

    char opts[1024];
    /* lowerdir is the prebuilt base; upper/work are per-container */
    snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s",
	     base_root, upper, work);

    if (mount("overlay", merged, "overlay", 0, opts) != 0) {
	perror("mount overlay failed");
	return -1;
    }

    /* copy merged path to output */
    strncpy(merged_out, merged, merged_len - 1);
    merged_out[merged_len - 1] = '\0';
    return 0;
}

/* Child entrypoint. It will block reading merged path from sync_pipe[0] until parent writes it. */
int child_main(void *arg)
{
    const char *shell = (const char *) arg;
    char merged_path[512] = { 0 };

    /* read merged root path from parent via pipe */
    ssize_t r = read(sync_pipe[0], merged_path, sizeof(merged_path) - 1);
    if (r <= 0) {
	fprintf(stderr,
		"[child] failed to read merged path from parent: %s\n",
		strerror(errno));
	return 1;
    }
    /* close read end after reading */
    close(sync_pipe[0]);
    sync_pipe[0] = -1;

    printf("[sandbox child] pid=%d got merged root: %s\n", getpid(),
	   merged_path);

    /* Make mount private in child's mount namespace */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
	perror("mount private");

    /* Setup minimal /dev inside chroot (creates /dev/* within merged path after chroot) */
    setup_minimal_dev();

    /* chroot into the provided merged root and chdir to / */
    if (chroot(merged_path) != 0) {
	perror("chroot failed");
	return 1;
    }
    if (chdir("/") != 0) {
	perror("chdir failed");
	return 1;
    }

    /* Mount proc inside container */
    mkdir_p("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, "") != 0)
	perror("mount /proc failed");

    /* set a container hostname in the UTS namespace */
    if (sethostname("sandbox", strlen("sandbox")) != 0)
	perror("sethostname");

    /* finally exec the requested shell/program */
    char *const args[] = { (char *) shell, NULL };
    execv(args[0], args);
    perror("execv failed");
    return 1;
}

int main(int argc, char *argv[])
{
    const char *shell = "/bin/sh";
    if (argc > 1)
	shell = argv[1];

    /* create sync pipe (parent -> child) */
    if (pipe(sync_pipe) != 0) {
	perror("pipe");
	exit(1);
    }

    /* flags for clone: create new namespaces */
    int flags = CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNS |
	CLONE_NEWPID | CLONE_NEWNET | SIGCHLD;

    printf("[host] Spawning isolated process...\n");

    /* spawn child; child inherits sync_pipe fds */
    pid_t child =
	clone(child_main, child_stack + STACK_SIZE, flags, (void *) shell);
    if (child < 0) {
	perror("clone failed");
	exit(1);
    }

    /* parent: close the read end; we'll write merged path to write end */
    close(sync_pipe[0]);
    sync_pipe[0] = -1;

    /* Setup UID/GID mapping for user namespace so child UID 0 maps to caller's uid */
    char map_file[256];
    char map[128];

    snprintf(map_file, sizeof(map_file), "/proc/%d/uid_map", child);
    FILE *f = fopen(map_file, "w");
    if (f) {
	snprintf(map, sizeof(map), "0 %d 1\n", getuid());
	fwrite(map, 1, strlen(map), f);
	fclose(f);
    } else {
	perror("open uid_map");
    }

    snprintf(map_file, sizeof(map_file), "/proc/%d/setgroups", child);
    f = fopen(map_file, "w");
    if (f) {
	fwrite("deny\n", 1, 5, f);
	fclose(f);
    }				/* deny setgroups if supported */

    snprintf(map_file, sizeof(map_file), "/proc/%d/gid_map", child);
    f = fopen(map_file, "w");
    if (f) {
	snprintf(map, sizeof(map), "0 %d 1\n", getgid());
	fwrite(map, 1, strlen(map), f);
	fclose(f);
    } else {
	perror("open gid_map");
    }

    /* Setup per-container overlay using the child pid (so path is unique) */
    char merged_root[512];
    if (setup_overlay_root_for_pid(child, merged_root, sizeof(merged_root))
	!= 0) {
	fprintf(stderr, "[host] overlay setup failed; killing child\n");
	kill(child, SIGKILL);
	close(sync_pipe[1]);
	sync_pipe[1] = -1;
	waitpid(child, NULL, 0);
	exit(1);
    }

    printf("[host] overlay root mounted at %s\n", merged_root);

    /* Setup cgroups for the child */
    setup_cgroup(child);

    /* send merged root path to child over pipe, then close write end */
    size_t to_write = strlen(merged_root) + 1;
    if (write(sync_pipe[1], merged_root, to_write) != (ssize_t) to_write) {
	perror("[host] write merged path failed");
	/* attempt cleanup and abort */
	close(sync_pipe[1]);
	sync_pipe[1] = -1;
	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
	exit(1);
    }
    close(sync_pipe[1]);
    sync_pipe[1] = -1;

    /* Wait for child to exit */
    int status;
    waitpid(child, &status, 0);

    /* Cleanup overlay mount and container dir */
    printf("[host] cleaning up overlay at %s\n", merged_root);
    if (umount(merged_root) != 0)
	perror("umount merged_root");
    /* remove container directory tree (upper, work, rootfs) */
    char rmcmd[1024];
    snprintf(rmcmd, sizeof(rmcmd), "/bin/sh -c 'rm -rf %s/%d'",
	     container_base, (int) child);
    system(rmcmd);

    if (WIFEXITED(status))
	printf("[host] sandbox exited code=%d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
	printf("[host] sandbox killed by signal %d\n", WTERMSIG(status));
    else
	printf("[host] sandbox ended\n");

    return 0;
}
