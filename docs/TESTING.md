# Testing Guide for feather_virt

## Quick Start

```bash
# Run all unit tests (fast, no side effects)
zig build test
```

## Test Organization

### Unit Tests on C Files
Fast tests that don't modify system state:

- `src/cgroup_test.zig` - Cgroup pure logic tests

## Running Specific Tests

```bash
# Run tests matching a filter
zig build test -- --test-filter "cgroup"

# Run a specific test
zig build test -- --test-filter "build_cgroup_path with name"

# Verbose output
zig build test -- --verbose
```
## Test Structure

All C-to-Zig test files follow this pattern:

```zig
const std = @import("std");
const testing = std.testing;

// Import C functions
extern fn your_function(arg: c_int) void;

test "descriptive test name" {
    // Arrange
    var buf: [128]u8 = undefined;
    
    // Act
    your_function(42);
    
    // Assert
    try testing.expectEqual(expected, actual);
}
```

## Debugging Tests

```bash
# Run with debug output
zig build test --summary all

# Run a single test with verbose output
zig build test -- --test-filter "your test" --verbose

# Build test executable without running
zig build test --no-run
```

## Current Status

### Implemented
- Build system for C tests
- Cgroup unit test

## Migration Path to Pure Zig

When translating C code to Zig:

1. Pure Zig tests are much simpler (no `extern` declarations, same file)
2. Use Zig's allocator in tests: `std.testing.allocator`
3. Leverage Zig's error handling

Example:
```zig
test "zig function" {
    const allocator = std.testing.allocator;
    const result = try buildCgroupPath(allocator, "/base", "name");
    defer allocator.free(result);
    try testing.expectEqualStrings("/base-name", result);
}
```
