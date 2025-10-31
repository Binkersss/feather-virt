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
const char *new_root = "/tmp/sandbox_root";
const char *cgroup_base = "/sys/fs/cgroup/sandbox1";

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

// Create minimal /dev inside sandbox
static void setup_minimal_dev() {
    mkdir_p("/dev", 0755);
    mknod("/dev/null", S_IFCHR | 0666, makedev(1,3));
    mknod("/dev/zero", S_IFCHR | 0666, makedev(1,5));

    mkdir_p("/dev/pts", 0755);
    mount("devpts", "/dev/pts", "devpts", 0, NULL);

    mknod("/dev/ptmx", S_IFCHR | 0666, makedev(5,2));
}

// Setup cgroup v2 for the child PID
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

int child_main(void *arg) {
    const char *shell = (const char*)arg;

    printf("[sandbox] child pid=%d\n", getpid());

    // Make mount private
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
        perror("mount private");

    // Setup minimal /dev
    setup_minimal_dev();

    // chroot + chdir
    if (chroot(new_root) != 0) { perror("chroot failed"); return 1; }
    if (chdir("/") != 0) { perror("chdir failed"); return 1; }

    // Mount /proc
    mkdir_p("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, "") != 0)
        perror("mount /proc failed");

    // Set hostname (UTS namespace)
    sethostname("sandbox", strlen("sandbox"));

    // Drop into shell
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

    // Setup UID/GID mapping for user namespace
    char map_file[256], map[64];
    snprintf(map_file, sizeof(map_file), "/proc/%d/uid_map", pid);
    FILE *f = fopen(map_file, "w");
    if (f) {
        snprintf(map, sizeof(map), "0 %d 1\n", getuid());
        fwrite(map, 1, strlen(map), f);
        fclose(f);
    }

    snprintf(map_file, sizeof(map_file), "/proc/%d/gid_map", pid);
    f = fopen(map_file, "w");
    if (f) {
        snprintf(map, sizeof(map), "0 %d 1\n", getgid());
        fwrite(map, 1, strlen(map), f);
        fclose(f);
    }

    // Setup cgroups
    setup_cgroup(pid);

    // Wait for child
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) printf("[host] sandbox exited code=%d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) printf("[host] sandbox killed by signal %d\n", WTERMSIG(status));
    else printf("[host] sandbox ended\n");

    return 0;
}

