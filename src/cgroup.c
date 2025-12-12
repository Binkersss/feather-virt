#define _GNU_SOURCE
#include "cgroup.h"
#include "overlay.h"
#include <stdio.h>
#include <string.h>

// Redefining bc it's simpler ikik it's not good
#define MAX_PATH_LEN 4096

void build_cgroup_path(const char *base, const char *name, char *output, size_t output_len) {
    if (name && strlen(name) > 0 && strcmp(name, "unnamed") != 0) {
        snprintf(output, output_len, "%s-%s", base, name);
    } else {
        strncpy(output, base, output_len - 1);
        output[output_len - 1] = '\0';
    }
}

void build_memory_limit_string(unsigned long long mb, char *output, size_t output_len) {
    snprintf(output, output_len, "%llu", mb * 1024ULL * 1024ULL);
}

void build_cpu_limit_string(int percent, char *output, size_t output_len) {
    // percent = 50 means 50% of one core
    // Format: "quota period" where quota/period = percentage
    // Standard period is 100000 (100ms)
    int quota = percent * 1000;
    snprintf(output, output_len, "%d 100000", quota);
}

void build_pids_limit_string(int max_pids, char *output, size_t output_len) {
    snprintf(output, output_len, "%d", max_pids);
}

int write_cgroup_file(const char *cgroup_path, const char *filename, const char *value) {
    char file_path[MAX_PATH_LEN + 32];
    snprintf(file_path, sizeof(file_path), "%s/%s", cgroup_path, filename);
    
    FILE *f = fopen(file_path, "w");
    if (!f) {
        perror(file_path);
        return -1;
    }
    
    fprintf(f, "%s", value);
    fclose(f);
    return 0;
}

void setup_cgroup(const container_config_t *cfg) {
    char cgroup_path[MAX_PATH_LEN];
    char value_buf[128];
    
    // Build cgroup path
    build_cgroup_path(cfg->cgroup_base, cfg->name, cgroup_path, sizeof(cgroup_path));
    mkdir_p(cgroup_path, 0755);
    
    // Set memory limit
    build_memory_limit_string(128, value_buf, sizeof(value_buf));
    write_cgroup_file(cgroup_path, "memory.max", value_buf);
    
    // Set CPU limit
    build_cpu_limit_string(50, value_buf, sizeof(value_buf));
    write_cgroup_file(cgroup_path, "cpu.max", value_buf);
    
    // Set PID limit
    build_pids_limit_string(10, value_buf, sizeof(value_buf));
    write_cgroup_file(cgroup_path, "pids.max", value_buf);
    
    // Add process to cgroup
    snprintf(value_buf, sizeof(value_buf), "%d", cfg->pid);
    write_cgroup_file(cgroup_path, "cgroup.procs", value_buf);
}
