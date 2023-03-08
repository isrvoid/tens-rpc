const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});
    const trpc = b.addStaticLibrary(.{
        .name = "tens_rpc",
        .target = target,
        .optimize = optimize,
    });
    trpc.addCSourceFile("src/temp_dummy.c", &.{});
    trpc.addIncludePath("src");
    trpc.linkLibC();

    const exe_tests = b.addTest(.{
        .root_source_file = .{ .path = "src/test_runner.zig" },
        .target = target,
        .optimize = optimize,
    });
    exe_tests.linkLibrary(trpc);
    exe_tests.addIncludePath("src");
    const test_step = b.step("test", "Run unit tests");
    test_step.dependOn(&exe_tests.step);
}
