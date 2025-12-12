const std = @import("std");
const testing = std.testing;

// Import the testable helper functions
extern fn build_uid_map_content(buf: [*]u8, buf_len: usize) c_int;
extern fn build_gid_map_content(buf: [*]u8, buf_len: usize) c_int;
extern fn build_proc_path(pid: c_int, filename: [*:0]const u8, buf: [*]u8, buf_len: usize) void;

test "namespace - build_uid_map_content format" {
    var buf: [128]u8 = undefined;

    const result = build_uid_map_content(&buf, buf.len);
    try testing.expect(result > 0);

    const content = buf[0..@as(usize, @intCast(result))];

    // Should be in format "0 <uid> 1\n"
    try testing.expect(std.mem.startsWith(u8, content, "0 "));
    try testing.expect(std.mem.endsWith(u8, content, " 1\n"));
}

test "namespace - build_gid_map_content format" {
    var buf: [128]u8 = undefined;

    const result = build_gid_map_content(&buf, buf.len);
    try testing.expect(result > 0);

    const content = buf[0..@as(usize, @intCast(result))];

    // Should be in format "0 <gid> 1\n"
    try testing.expect(std.mem.startsWith(u8, content, "0 "));
    try testing.expect(std.mem.endsWith(u8, content, " 1\n"));
}

test "namespace - build_proc_path for uid_map" {
    var buf: [256]u8 = undefined;

    build_proc_path(12345, "uid_map", &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("/proc/12345/uid_map", result);
}

test "namespace - build_proc_path for gid_map" {
    var buf: [256]u8 = undefined;

    build_proc_path(67890, "gid_map", &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("/proc/67890/gid_map", result);
}

test "namespace - build_proc_path for setgroups" {
    var buf: [256]u8 = undefined;

    build_proc_path(99999, "setgroups", &buf, buf.len);

    const result = std.mem.sliceTo(&buf, 0);
    try testing.expectEqualStrings("/proc/99999/setgroups", result);
}

test "namespace - buffer truncation safety" {
    var small_buf: [10]u8 = undefined;

    // These should truncate safely without crashing
    _ = build_uid_map_content(&small_buf, small_buf.len);
    _ = build_gid_map_content(&small_buf, small_buf.len);
    build_proc_path(123, "uid_map", &small_buf, small_buf.len);

    try testing.expect(true);
}

test "namespace - uid/gid content includes newline" {
    var buf: [128]u8 = undefined;

    const uid_len = build_uid_map_content(&buf, buf.len);
    const uid_content = buf[0..@as(usize, @intCast(uid_len))];
    try testing.expect(uid_content[uid_content.len - 1] == '\n');

    const gid_len = build_gid_map_content(&buf, buf.len);
    const gid_content = buf[0..@as(usize, @intCast(gid_len))];
    try testing.expect(gid_content[gid_content.len - 1] == '\n');
}
