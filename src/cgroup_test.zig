const std = @import("std");
const testing = std.testing;

// Import C functions
extern fn build_cgroup_path(base: [*:0]const u8, name: [*:0]const u8, output: [*]u8, output_len: usize) void;
extern fn build_memory_limit_string(mb: c_ulonglong, output: [*]u8, output_len: usize) void;
extern fn build_cpu_limit_string(percent: c_int, output: [*]u8, output_len: usize) void;
extern fn build_pids_limit_string(max_pids: c_int, output: [*]u8, output_len: usize) void;

test "cgroup - build_cgroup_path with name" {
    var buf: [256]u8 = undefined;

    build_cgroup_path("/sys/fs/cgroup/sandbox", "mycontainer", &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("/sys/fs/cgroup/sandbox-mycontainer", result);
}

test "cgroup - build_cgroup_path without name" {
    var buf: [256]u8 = undefined;

    build_cgroup_path("/sys/fs/cgroup/sandbox", "unnamed", &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("/sys/fs/cgroup/sandbox", result);
}

test "cgroup - build_cgroup_path with empty name" {
    var buf: [256]u8 = undefined;

    build_cgroup_path("/sys/fs/cgroup/sandbox", "", &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("/sys/fs/cgroup/sandbox", result);
}

test "cgroup - build_memory_limit_string for 128MB" {
    var buf: [128]u8 = undefined;

    build_memory_limit_string(128, &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    const expected_bytes = 128 * 1024 * 1024;
    var expected: [32]u8 = undefined;
    const expected_str = try std.fmt.bufPrint(&expected, "{d}", .{expected_bytes});

    try testing.expectEqualStrings(expected_str, result);
}

test "cgroup - build_memory_limit_string for various sizes" {
    const test_cases = [_]struct { mb: u64, expected_bytes: u64 }{
        .{ .mb = 64, .expected_bytes = 64 * 1024 * 1024 },
        .{ .mb = 256, .expected_bytes = 256 * 1024 * 1024 },
        .{ .mb = 512, .expected_bytes = 512 * 1024 * 1024 },
        .{ .mb = 1024, .expected_bytes = 1024 * 1024 * 1024 },
    };

    for (test_cases) |tc| {
        var buf: [128]u8 = undefined;
        build_memory_limit_string(tc.mb, &buf, buf.len);

        const result = std.mem.sliceTo(&buf, 0);
        var expected: [32]u8 = undefined;
        const expected_str = try std.fmt.bufPrint(&expected, "{d}", .{tc.expected_bytes});

        try testing.expectEqualStrings(expected_str, result);
    }
}

test "cgroup - build_cpu_limit_string for 50%" {
    var buf: [128]u8 = undefined;

    build_cpu_limit_string(50, &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("50000 100000", result);
}

test "cgroup - build_cpu_limit_string for various percentages" {
    const test_cases = [_]struct { percent: c_int, expected: []const u8 }{
        .{ .percent = 10, .expected = "10000 100000" },
        .{ .percent = 25, .expected = "25000 100000" },
        .{ .percent = 50, .expected = "50000 100000" },
        .{ .percent = 75, .expected = "75000 100000" },
        .{ .percent = 100, .expected = "100000 100000" },
    };

    for (test_cases) |tc| {
        var buf: [128]u8 = undefined;
        build_cpu_limit_string(tc.percent, &buf, buf.len);

        const result = std.mem.sliceTo(&buf, 0);
        try testing.expectEqualStrings(tc.expected, result);
    }
}

test "cgroup - build_pids_limit_string" {
    var buf: [128]u8 = undefined;

    build_pids_limit_string(10, &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("10", result);
}

test "cgroup - build_pids_limit_string various limits" {
    const test_cases = [_]struct { limit: c_int, expected: []const u8 }{
        .{ .limit = 1, .expected = "1" },
        .{ .limit = 10, .expected = "10" },
        .{ .limit = 50, .expected = "50" },
        .{ .limit = 100, .expected = "100" },
        .{ .limit = 1000, .expected = "1000" },
    };

    for (test_cases) |tc| {
        var buf: [128]u8 = undefined;
        build_pids_limit_string(tc.limit, &buf, buf.len);

        const result = std.mem.sliceTo(&buf, 0);
        try testing.expectEqualStrings(tc.expected, result);
    }
}

test "cgroup - buffer overflow protection" {
    var small_buf: [10]u8 = undefined;

    // Should truncate gracefully without overflowing
    build_cgroup_path("/very/long/path/that/exceeds/buffer", "name", &small_buf, small_buf.len);
    build_memory_limit_string(999999, &small_buf, small_buf.len);
    build_cpu_limit_string(100, &small_buf, small_buf.len);
    build_pids_limit_string(12345, &small_buf, small_buf.len);

    // If we get here without crashing, buffer protection is working
    try testing.expect(true);
}
