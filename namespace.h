#ifndef NAMESPACE_H
#define NAMESPACE_H

#include <sys/types.h>

/* Setup UID/GID mapping for user namespace */
void setup_uid_gid_map(pid_t child);

/* Create minimal /dev inside sandbox */
void setup_minimal_dev(void);

#endif /* NAMESPACE_H */
