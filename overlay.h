#ifndef OVERLAY_H
#define OVERLAY_H

#include "config.h"

/* Create overlay mount for container */
int setup_overlay_root(const container_config_t * cfg, char *merged_out,
		       size_t merged_len);

/* Cleanup overlay mount and container directory */
void cleanup_overlay(const container_config_t * cfg,
		     const char *merged_root);

/* Helper to create directories recursively */
void mkdir_p(const char *path, mode_t mode);

#endif				/* OVERLAY_H */
