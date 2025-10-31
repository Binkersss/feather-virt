#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

// Helper to make directories
static void mkdir_p(const char *path, mode_t mode) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    mkdir(tmp, mode);
}

int child_main(void *arg) {
    const char *shell = (const char*)arg;
    const char *new_root = "/tmp/sandbox_root";

    printf("[sandbox] child pid=%d\n", getpid());

    // Make mount propagation private
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        perror("mount private failed");
    }

    // Create minimal dirs inside new root if not present
    mkdir_p("/tmp/sandbox_root/proc", 0755);
    mkdir_p("/tmp/sandbox_root/dev", 0755);
    mkdir_p("/tmp/sandbox_root/sys", 0755);
    mkdir_p("/tmp/sandbox_root/tmp", 1777);

    // Bind-mount /dev from host so shell can access TTY
    if (mount("/dev", "/tmp/sandbox_root/dev", NULL, MS_BIND | MS_REC, NULL) != 0) {
        perror("bind /dev failed");
    }

    // chroot + chdir
    if (chroot(new_root) != 0) { perror("chroot failed"); return 1; }
    if (chdir("/") != 0) { perror("chdir failed"); return 1; }

    // Mount /proc inside chroot
    if (mount("proc", "/proc", "proc", 0, "") != 0) {
        perror("mount /proc failed");
    }

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

    int flags = CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWPID |
                CLONE_NEWNET | SIGCHLD;

    printf("[host] Spawning isolated process (Alpine rootfs)...\n");
    pid_t pid = clone(child_main, child_stack + STACK_SIZE, flags, (void*)shell);

    if (pid < 0) { perror("clone failed"); exit(1); }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid failed"); }
    else if (WIFEXITED(status)) { printf("[host] sandbox exited, code=%d\n", WEXITSTATUS(status)); }
    else if (WIFSIGNALED(status)) { printf("[host] sandbox killed by signal %d\n", WTERMSIG(status)); }
    else { printf("[host] sandbox ended (status=%d)\n", status); }

    printf("[host] done\n");
    return 0;
}

