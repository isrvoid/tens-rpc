const std = @import("std");

pub fn main() void {
}

test "allocator" {
    @import("test_allocator.zig").enforceInclude();
}
