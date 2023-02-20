const std = @import("std");
const mem = std.mem;
const expect = std.testing.expect;
const c = @cImport({ @cInclude("tree_allocator.h"); });

pub fn enforceInclude() void {}

var buf: [0x1000]u8 = undefined;

fn initMember(min_blocks: usize) c.tral_member_t {
    std.debug.assert(buf.len >= c.tral_required_member_buffer_size(min_blocks));
    var res: c.tral_member_t = undefined;
    c.tral_init_member(&res, min_blocks, &buf);
    return res;
}

test "init" {
    const min_blocks = 8;
    const buffer_size = c.tral_required_member_buffer_size(min_blocks);
    try expect(buffer_size > 0);
    if (buffer_size > buf.len) @panic("");
    var m: c.tral_member_t = undefined;
    c.tral_init_member(&m, min_blocks, &buf);
    try expect(c.tral_num_blocks(&m) >= min_blocks);
}

test "mark" {
    var m = initMember(2);
    var adr: u32 = undefined;
    try expect(c.tral_mark(&m, 1, &adr));
    try expect(0 == adr);
    try expect(c.tral_mark(&m, 1, &adr));
    try expect(1 == adr);
}

test "clear" {
    var m = initMember(1);
    var adr: u32 = undefined;
    _ = c.tral_mark(&m, 1, &adr);
    c.tral_clear(&m, adr, 1);
    // cleared block is reused
    try expect(c.tral_mark(&m, 1, &adr));
    try expect(0 == adr);
}

test "mark larger size" {
    var m = initMember(4);
    var adr: u32 = undefined;
    try expect(c.tral_mark(&m, 2, &adr));
    try expect(0 == adr);
    try expect(c.tral_mark(&m, 2, &adr));
    try expect(2 == adr);
}

test "mark increasing size" {
    var m = initMember(3);
    var adr: u32 = undefined;
    _ = c.tral_mark(&m, 1, &adr);
    try expect(c.tral_mark(&m, 2, &adr));
    try expect(2 == adr);
}

test "mark decreasing size" {
    var m = initMember(3);
    var adr: u32 = undefined;
    _ = c.tral_mark(&m, 2, &adr);
    try expect(c.tral_mark(&m, 1, &adr));
    try expect(2 == adr);
}

test "clear previous" {
    var m = initMember(3);
    var adr: u32 = undefined;
    _ = c.tral_mark(&m, 1, &adr);
    _ = c.tral_mark(&m, 1, &adr);
    c.tral_clear(&m, 0, 1);
    _ = c.tral_mark(&m, 1, &adr);
    try expect(c.tral_mark(&m, 1, &adr));
    try expect(2 == adr);
}

test "full" {
    var m = initMember(1234);
    var adr: u32 = undefined;
    var num_free_blocks: usize = c.tral_num_blocks(&m);
    while (num_free_blocks > 0) : (num_free_blocks -= 1)
        try expect(c.tral_mark(&m, 1, &adr));
    try expect(!c.tral_mark(&m, 1, &adr));
    // mark succeeds again after clearing
    c.tral_clear(&m, 3, 1);
    try expect(c.tral_mark(&m, 1, &adr));
    try expect(3 == adr);
}

test "member buffer size" {
    const min_block_count = 123;
    const req_buf_len = c.tral_required_member_buffer_size(min_block_count);
    const sentinel = buf[req_buf_len..][0..4];
    const sentinel_val: u32 = 0xcafe7e57;
    mem.copy(u8, sentinel, &mem.toBytes(sentinel_val));
    var m = initMember(min_block_count);
    var adr: u32 = undefined;
    var i: u32 = 0;
    while (c.tral_mark(&m, i % 8 + 1, &adr)) : (i += 1) {} // fill with different sizes until it fails
    while (c.tral_mark(&m, 1, &adr)) {} // fill any remaining blocks
    // sentinel past the claimed buffer length is untouched
    try expect(sentinel_val == mem.bytesToValue(u32, sentinel));
}
