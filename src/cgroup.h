#ifndef CGROUP_H
#define CGROUP_H

#include "config.h"

// Pure functions that can be easily unit tested
void build_cgroup_path(const char *base, const char *name, char *output, size_t output_len);
void build_memory_limit_string(unsigned long long mb, char *output, size_t output_len);
void build_cpu_limit_string(int percent, char *output, size_t output_len);
void build_pids_limit_string(int max_pids, char *output, size_t output_len);

// File operations (can be mocked or tested with temp directories)
int write_cgroup_file(const char *cgroup_path, const char *filename, const char *value);

/* Setup cgroup v2 limits for container */
void setup_cgroup(const container_config_t * cfg);

#endif				/* CGROUP_H */
