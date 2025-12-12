#define _GNU_SOURCE
#include "overlay.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int mkdir_p(const char *path, mode_t mode)
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
    return mkdir(tmp, mode);
}

int setup_overlay_root(const container_config_t * cfg, char *merged_out,
		       size_t merged_len)
{
    char upper[MAX_PATH_LEN], work[MAX_PATH_LEN], merged[MAX_PATH_LEN];
    char container_dir[MAX_PATH_LEN];
    char opts[1024];

    /* Create per-container directories using name if provided, otherwise PID */
    if (strlen(cfg->name) > 0 && strcmp(cfg->name, "unnamed") != 0) {
	snprintf(container_dir, sizeof(container_dir), "%s/%s-%d",
		 cfg->container_base, cfg->name, (int) cfg->pid);
    } else {
	snprintf(container_dir, sizeof(container_dir), "%s/%d",
		 cfg->container_base, (int) cfg->pid);
    }

    snprintf(upper, sizeof(upper), "%s/upper", container_dir);
    snprintf(work, sizeof(work), "%s/work", container_dir);
    snprintf(merged, sizeof(merged), "%s/rootfs", container_dir);

    mkdir_p(cfg->container_base, 0755);
    mkdir_p(container_dir, 0755);
    mkdir_p(upper, 0755);
    mkdir_p(work, 0755);
    mkdir_p(merged, 0755);

    /* lowerdir is the base image; upper/work are per-container */
    snprintf(opts, sizeof(opts), "lowerdir=%s,upperdir=%s,workdir=%s",
	     cfg->base_root, upper, work);

    if (mount("overlay", merged, "overlay", 0, opts) != 0) {
	perror("mount overlay failed");
	return -1;
    }

    strncpy(merged_out, merged, merged_len - 1);
    merged_out[merged_len - 1] = '\0';
    return 0;
}

void cleanup_overlay(const container_config_t * cfg,
		     const char *merged_root)
{
    char rmcmd[1024];
    char container_dir[MAX_PATH_LEN];

    printf("[host] cleaning up overlay at %s\n", merged_root);

    if (umount(merged_root) != 0) {
	perror("umount merged_root");
    }

    /* Build container directory path */
    if (strlen(cfg->name) > 0 && strcmp(cfg->name, "unnamed") != 0) {
	snprintf(container_dir, sizeof(container_dir), "%s/%s-%d",
		 cfg->container_base, cfg->name, (int) cfg->pid);
    } else {
	snprintf(container_dir, sizeof(container_dir), "%s/%d",
		 cfg->container_base, (int) cfg->pid);
    }

    /* Remove container directory tree */
    snprintf(rmcmd, sizeof(rmcmd), "/bin/sh -c 'rm -rf %s'",
	     container_dir);
    system(rmcmd);
}
