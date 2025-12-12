// This file is here for now to just let the Zig build system do its thing
const std = @import("std");

pub fn main() !void {
    // Get command line arguments
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    // Convert Zig args to C-style args
    const c_argv = try allocator.alloc([*:0]u8, args.len);
    for (args, 0..) |arg, i| {
        c_argv[i] = @ptrCast(arg.ptr);
    }

    // Call C main (declared in main.c)
    const exit_code = c_main(@intCast(args.len), c_argv.ptr);
    std.process.exit(@intCast(exit_code));
}

// Declare the C main function
extern "c" fn c_main(argc: c_int, argv: [*][*:0]u8) c_int;
