#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <time.h>

#include "config.h"
#include "overlay.h"
#include "cgroup.h"
#include "namespace.h"

#define STACK_SIZE (1024*1024)
static char child_stack[STACK_SIZE];

/* Sync pipe: parent writes merged path; child reads it */
static int sync_pipe[2] = { -1, -1 };

/* Global config passed to child */
static container_config_t *g_config = NULL;

// Debug flag
static int debug_mode = 0;

/* Child entrypoint */
int child_main(void *arg)
{
    container_config_t *cfg = (container_config_t *) arg;
    char merged_path[MAX_PATH_LEN] = { 0 };

    /* Read merged root path from parent via pipe */
    ssize_t r = read(sync_pipe[0], merged_path, sizeof(merged_path) - 1);
    if (r <= 0) {
	fprintf(stderr,
		"[child] failed to read merged path from parent\n");
	return 1;
    }
    close(sync_pipe[0]);
    sync_pipe[0] = -1;

    printf("[sandbox child] pid=%d container='%s' merged root: %s\n",
	   getpid(), cfg->name, merged_path);

    /* Make mount private in child's mount namespace */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) != 0)
	perror("mount private");

    /* Setup minimal /dev inside chroot */
    setup_minimal_dev();

    /* chroot into merged root */
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

    /* Set container hostname */
    if (sethostname(cfg->name, strlen(cfg->name)) != 0)
	perror("sethostname");

    /* Execute shell */
    char *const args[] = { (char *) cfg->shell, NULL };
    execv(args[0], args);
    perror("execv failed");
    return 1;
}

void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf
	("  --image <name>        Select base rootfs image (required unless listing)\n");
    printf
	("  --list-images         Show available base images and exit\n");
    printf("  --list-containers     Show running containers\n");
    printf
	("  --list-all            Show all containers (running and stopped)\n");
    printf
	("  --shell <path>        Shell to execute (default: /bin/sh)\n");
    printf
	("  --name <name>         Human-readable container name (default: unnamed)\n");
    printf
	("  --debug               Preserve container directory after exit\n");
    printf("  -h, --help            Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s --image alpine-3.20.2\n", prog);
    printf("  %s --image alpine-3.20.2 --name test1 --debug\n", prog);
    printf("  %s --list-containers\n", prog);
    printf("  %s --list-all\n", prog);
}

int c_main(int argc, char *argv[])
{
    container_config_t config;
    const char *image_name = NULL;
    int list_images = 0;

    config_init(&config);

    /* Parse command-line arguments */
    static struct option long_options[] = {
	{ "image", required_argument, 0, 'i' },
	{ "list-images", no_argument, 0, 'l' },
	{ "list-containers", no_argument, 0, 'L' },
	{ "list-all", no_argument, 0, 'A' },
	{ "debug", no_argument, 0, 'd' },
	{ "shell", required_argument, 0, 's' },
	{ "name", required_argument, 0, 'n' },
	{ "help", no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
    };

    int opt;
    int list_containers = 0;
    int list_all = 0;
    while ((opt =
	    getopt_long(argc, argv, "i:ls:n:h", long_options,
			NULL)) != -1) {
	switch (opt) {
	case 'i':
	    image_name = optarg;
	    break;
	case 'l':
	    list_images = 1;
	    break;
	case 'L':
	    list_containers = 1;
	    break;
	case 'A':
	    list_containers = 1;
	    list_all = 1;
	    break;
	case 'd':
	    debug_mode = 1;
	    break;
	case 's':
	    strncpy(config.shell, optarg, MAX_PATH_LEN - 1);
	    break;
	case 'n':
	    strncpy(config.name, optarg, MAX_NAME_LEN - 1);
	    break;
	case 'h':
	    print_usage(argv[0]);
	    return 0;
	default:
	    print_usage(argv[0]);
	    return 1;
	}
    }

    if (list_containers) {
	config_list_containers(list_all);
	return 0;
    }

    /* Handle --list-images */
    if (list_images) {
	config_list_images(NULL);
	return 0;
    }

    /* Validate that image was specified */
    //if (!image_name) {
    //fprintf(stderr, "Error: --image is required\n\n");
    //print_usage(argv[0]);
    //return 1;
    //}

    /* Set and validate image */
    if (image_name && config_set_image(&config, image_name) != 0) {
	fprintf(stderr, "\nUse --list-images to see available images\n");
	return 1;
    }
    // After config setup and before spawning child
    config.created_at = time(NULL);
    strncpy(config.status, "starting", sizeof(config.status) - 1);

    // Set default limits (can be overridden by CLI flags)
    config.limits.memory_bytes = 128 * 1024 * 1024;	// 128MB
    config.limits.cpu_quota = 50000;	// 50% of one core
    config.limits.pids_max = 10;

    printf("[host] Configuration:\n");
    printf("  Image:     %s\n", config.base_root);
    printf("  Shell:     %s\n", config.shell);
    printf("  Name:      %s\n", config.name);
    printf("\n");

    /* Create sync pipe */
    if (pipe(sync_pipe) != 0) {
	perror("pipe");
	exit(1);
    }

    /* Clone flags for namespaces */
    int flags = CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNS |
	CLONE_NEWPID | CLONE_NEWNET | SIGCHLD;

    printf("[host] Spawning isolated container...\n");

    /* Spawn child */
    g_config = &config;
    pid_t child = clone(child_main, child_stack + STACK_SIZE, flags,
			(void *) &config);
    if (child < 0) {
	perror("clone failed");
	exit(1);
    }

    /* Store child PID in config */
    config.pid = child;

    // Create container directory
    snprintf(config.container_dir, sizeof(config.container_dir),
	     "/var/sandbox/containers/%d", config.pid);
    if (mkdir_p(config.container_dir, 0755) != 0) {
	fprintf(stderr, "Failed to create container directory\n");
	return 1;
    }
    snprintf(config.config_file, sizeof(config.config_file),
	     "%s/config.json", config.container_dir);

    /* Parent: close read end */
    close(sync_pipe[0]);
    sync_pipe[0] = -1;

    /* Setup UID/GID mapping */
    setup_uid_gid_map(child);

    /* Setup overlay filesystem */
    char merged_root[MAX_PATH_LEN];
    if (setup_overlay_root(&config, merged_root, sizeof(merged_root)) != 0) {
	fprintf(stderr, "[host] overlay setup failed; killing child\n");
	kill(child, SIGKILL);
	close(sync_pipe[1]);
	waitpid(child, NULL, 0);
	exit(1);
    }

    printf("[host] overlay root mounted at %s\n", merged_root);

    /* Setup cgroups */
    setup_cgroup(&config);

    strncpy(config.status, "running", sizeof(config.status) - 1);
    if (config_save_to_file(&config) != 0) {
	fprintf(stderr, "Warning: failed to save container metadata\n");
    }

    /* Send merged root path to child */
    size_t to_write = strlen(merged_root) + 1;
    if (write(sync_pipe[1], merged_root, to_write) != (ssize_t) to_write) {
	perror("[host] write merged path failed");
	close(sync_pipe[1]);
	kill(child, SIGKILL);
	waitpid(child, NULL, 0);
	exit(1);
    }
    close(sync_pipe[1]);
    sync_pipe[1] = -1;

    /* Wait for child to exit */
    int status;
    waitpid(child, &status, 0);

    if (WIFEXITED(status)) {
	config_update_status(&config, "exited", WEXITSTATUS(status));
	printf("[host] container '%s' exited code=%d\n", config.name,
	       WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
	config_update_status(&config, "killed", WTERMSIG(status));
	printf("[host] container '%s' killed by signal %d\n", config.name,
	       WTERMSIG(status));
    } else {
	config_update_status(&config, "stopped", -1);
	printf("[host] container '%s' ended\n", config.name);
    }

    /* Cleanup */
    cleanup_overlay(&config, merged_root);

    if (!debug_mode) {
	cleanup_overlay(&config, merged_root);
	// Remove container directory
	char cmd[MAX_PATH_LEN * 2];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", config.container_dir);
	system(cmd);
    } else {
	printf("[host] Debug mode: container data preserved at %s\n",
	       config.container_dir);
    }

    return 0;
}
