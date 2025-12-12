#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <sys/types.h>

// Helper functions with no side effects
int build_uid_map_content(char *, size_t);
int build_gid_map_content(char *, size_t);
void build_proc_path(pid_t pid, const char *filename, char *buf,
		     size_t buf_len);

// Helper function with side effects
int write_proc_file(pid_t pid, const char *filename, const char *content);


// Public API
/* Setup UID/GID mapping for user namespace */
void setup_uid_gid_map(pid_t child);

/* Create minimal /dev inside sandbox */
void setup_minimal_dev(void);

#endif				/* NAMESPACE_H */
