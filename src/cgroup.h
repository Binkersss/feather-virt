#ifndef CGROUP_H
#define CGROUP_H

#include "config.h"

/* Setup cgroup v2 limits for container */
void setup_cgroup(const container_config_t * cfg);

#endif				/* CGROUP_H */
