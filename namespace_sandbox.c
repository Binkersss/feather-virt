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
#include <fcntl.h>

#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

static void mkdir_p(const char *path, mode_t mode) {
    char tmp[4096];
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

    // 1) Make mount propagation private to avoid affecting host
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0) {
        perror("mount private failed");
        // non-fatal; continue
    }

    // 2) Create new root dir and mount tmpfs
    if (mkdir(new_root, 0755) && errno != EEXIST) {
        perror("mkdir new_root failed");
        return 1;
    }
    if (mount("tmpfs", new_root, "tmpfs", 0, "size=64M") != 0) {
        perror("mount tmpfs failed");
        return 1;
    }

    // 3) Create minimal dirs inside new root
    char pathbuf[512];
    snprintf(pathbuf, sizeof(pathbuf), "%s/proc", new_root); mkdir_p(pathbuf, 0755);
    snprintf(pathbuf, sizeof(pathbuf), "%s/dev", new_root); mkdir_p(pathbuf, 0755);
    snprintf(pathbuf, sizeof(pathbuf), "%s/sys", new_root); mkdir_p(pathbuf, 0755);
    snprintf(pathbuf, sizeof(pathbuf), "%s/tmp", new_root); mkdir_p(pathbuf, 1777);
    snprintf(pathbuf, sizeof(pathbuf), "%s/bin", new_root); mkdir_p(pathbuf, 0755);

    // 4) Bind-mount /dev from host so shell has terminals and devices
    char target_dev[512];
    snprintf(target_dev, sizeof(target_dev), "%s/dev", new_root);
    if (mount("/dev", target_dev, NULL, MS_BIND | MS_REC, NULL) != 0) {
        perror("bind-mount /dev failed");
        // Not fatal for a minimal shell in many environments but desirable
    }

    // Optional: copy a minimal shell binary into newroot/bin if you want isolation.
    // For now we will use existing host /bin/sh via chroot (it will be visible via bind-mounts).

    // 5) chroot + chdir
    if (chroot(new_root) != 0) {
        perror("chroot failed");
        return 1;
    }
    if (chdir("/") != 0) {
        perror("chdir / failed");
        return 1;
    }

    // 6) Mount /proc inside the chrooted environment
    if (mount("proc", "/proc", "proc", 0, "") != 0) {
        perror("mount /proc failed (inside new root)");
        // continue anyway
    }

    // 7) (Optional) set hostname (UTS namespace)
    sethostname("sandbox", strlen("sandbox"));

    // 8) Drop into shell
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

    printf("[host] Spawning isolated process (with new root)...\n");
    pid_t pid = clone(child_main, child_stack + STACK_SIZE, flags, (void*)shell);

    if (pid < 0) {
        perror("clone failed");
        exit(1);
    }

    // Wait for the child (the container) to exit
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid failed");
    } else {
        if (WIFEXITED(status)) {
            printf("[host] sandbox exited, code=%d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[host] sandbox killed by signal %d\n", WTERMSIG(status));
        } else {
            printf("[host] sandbox ended (status=%d)\n", status);
        }
    }

    // Note: tmpfs mounted at /tmp/sandbox_root exists only in child's mount namespace,
    // so typically the host won't see it (because of CLONE_NEWNS) — no cleanup here.
    printf("[host] done\n");
    return 0;
}

