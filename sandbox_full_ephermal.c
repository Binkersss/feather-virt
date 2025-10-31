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

// THIS DOESN'T WORK BC KERNEL RESTRICTING MOUNTING OVERLAYFS

const char *base_root = "/tmp/sandbox_root";
const char *cgroup_base = "/sys/fs/cgroup/sandbox1";
const char *tmp_rw = "/tmp/sandbox_rw";
const char *tmp_work = "/tmp/sandbox_work";

// Helper to create directories recursively
static void mkdir_p(const char *path, mode_t mode) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = tmp + 1;
    while (*p) {
        if (*p == '/') { *p = 0; mkdir(tmp, mode); *p = '/'; }
        p++;
    }
    mkdir(tmp, mode);
}

// Minimal /dev inside sandbox
static void setup_minimal_dev() {
    mkdir_p("/dev", 0755);
    mknod("/dev/null", S_IFCHR | 0666, makedev(1,3));
    mknod("/dev/zero", S_IFCHR | 0666, makedev(1,5));

    mkdir_p("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, NULL);

    mknod("/dev/ptmx", S_IFCHR | 0666, makedev(5,2));
}

// Cgroup setup
static void setup_cgroup(pid_t child) {
    if (mkdir(cgroup_base, 0755) && errno != EEXIST) perror("mkdir cgroup failed");

    char path[256];

    snprintf(path, sizeof(path), "%s/memory.max", cgroup_base);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d", 128*1024*1024); fclose(f); }

    snprintf(path, sizeof(path), "%s/cpu.max", cgroup_base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "50000 100000"); fclose(f); }

    snprintf(path, sizeof(path), "%s/pids.max", cgroup_base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "10"); fclose(f); }

    snprintf(path, sizeof(path), "%s/cgroup.procs", cgroup_base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "%d", child); fclose(f); }
}

// Mount ephemeral overlay root
static void setup_overlay() {
    mkdir_p(tmp_rw, 0755);
    mkdir_p(tmp_work, 0755);

    char opts[512];
    snprintf(opts, sizeof(opts),
             "lowerdir=%s,upperdir=%s,workdir=%s", base_root, tmp_rw, tmp_work);
    if (mount("overlay", "/", "overlay", MS_REC | 0, opts) != 0)
        perror("mount overlay failed");
}

int child_main(void *arg) {
    const char *shell = (const char*)arg;
    printf("[sandbox] child pid=%d\n", getpid());

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
        perror("mount private");

    setup_minimal_dev();

    setup_overlay();  // ephemeral writable root

    mkdir_p("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, "") != 0)
        perror("mount /proc failed");

    sethostname("sandbox", strlen("sandbox"));

    char *const args[] = {(char *)shell, NULL};
    execv(args[0], args);
    perror("execv failed");
    return 1;
}

int main(int argc, char *argv[]) {
    const char *shell = "/bin/sh";
    if (argc > 1) shell = argv[1];

    int flags = CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNS |
                CLONE_NEWPID | CLONE_NEWNET | SIGCHLD;

    printf("[host] Spawning isolated process...\n");

    pid_t pid = clone(child_main, child_stack + STACK_SIZE, flags, (void*)shell);
    if (pid < 0) { perror("clone failed"); exit(1); }

    // setgroups deny before gid_map
    char map_file[256], map[64];
    snprintf(map_file, sizeof(map_file), "/proc/%d/setgroups", pid);
    FILE *f = fopen(map_file, "w");
    if (f) { fwrite("deny", 1, 4, f); fclose(f); }

    // UID map
    snprintf(map_file, sizeof(map_file), "/proc/%d/uid_map", pid);
    f = fopen(map_file, "w");
    if (f) { snprintf(map, sizeof(map), "0 %d 1\n", getuid());
        fwrite(map, 1, strlen(map), f); fclose(f); }

    // GID map
    snprintf(map_file, sizeof(map_file), "/proc/%d/gid_map", pid);
    f = fopen(map_file, "w");
    if (f) { snprintf(map, sizeof(map), "0 %d 1\n", getgid());
        fwrite(map, 1, strlen(map), f); fclose(f); }

    setup_cgroup(pid);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) printf("[host] sandbox exited code=%d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) printf("[host] sandbox killed by signal %d\n", WTERMSIG(status));
    else printf("[host] sandbox ended\n");

    return 0;
}

