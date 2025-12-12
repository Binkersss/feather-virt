#define _GNU_SOURCE
#include "cgroup.h"
#include "overlay.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

void setup_cgroup(const container_config_t *cfg)
{
    char cgroup_path[MAX_PATH_LEN];
    char file_path[MAX_PATH_LEN + 32];
    FILE *f;

    /* Create cgroup directory with name suffix */
    if (strlen(cfg->name) > 0 && strcmp(cfg->name, "unnamed") != 0) {
	snprintf(cgroup_path, sizeof(cgroup_path), "%s-%s",
		 cfg->cgroup_base, cfg->name);
    } else {
	strncpy(cgroup_path, cfg->cgroup_base, sizeof(cgroup_path) - 1);
    }

    mkdir_p(cgroup_path, 0755);

    /* Set memory limit: 128MB */
    snprintf(file_path, sizeof(file_path), "%s/memory.max", cgroup_path);
    f = fopen(file_path, "w");
    if (f) {
	fprintf(f, "%llu", (unsigned long long) 128 * 1024 * 1024);
	fclose(f);
    } else {
	perror("cgroup: memory.max");
    }

    /* Set CPU limit: 50% of one core */
    snprintf(file_path, sizeof(file_path), "%s/cpu.max", cgroup_path);
    f = fopen(file_path, "w");
    if (f) {
	fprintf(f, "50000 100000");
	fclose(f);
    } else {
	perror("cgroup: cpu.max");
    }

    /* Set PID limit: max 10 processes */
    snprintf(file_path, sizeof(file_path), "%s/pids.max", cgroup_path);
    f = fopen(file_path, "w");
    if (f) {
	fprintf(f, "10");
	fclose(f);
    } else {
	perror("cgroup: pids.max");
    }

    /* Add child PID to cgroup */
    snprintf(file_path, sizeof(file_path), "%s/cgroup.procs", cgroup_path);
    f = fopen(file_path, "w");
    if (f) {
	fprintf(f, "%d", cfg->pid);
	fclose(f);
    } else {
	perror("cgroup: cgroup.procs");
    }
}
