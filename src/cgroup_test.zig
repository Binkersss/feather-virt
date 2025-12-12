const std = @import("std");
const testing = std.testing;

// Importing C functions
extern fn setup_cgroup(const container_config_t * cfg);
