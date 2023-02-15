const std = @import("std");
const assert = std.debug.assert;
const c = @cImport({ @cInclude("tree_allocator.h"); });

pub fn enforceInclude() void {}

var buf: [1 << 14]u8 = undefined;

fn initMember(min_blocks: usize) c.tral_member_t {
    assert(buf.len >= c.tral_required_member_buffer_size(min_blocks));
    var res: c.tral_member_t = undefined;
    c.tral_init_member(&res, min_blocks, &buf);
    return res;
}

test "init" {
    const min_blocks = 8;
    const buffer_size = c.tral_required_member_buffer_size(min_blocks);
    assert(buffer_size > 0);
    if (buffer_size > buf.len) @panic("");
    var m: c.tral_member_t = undefined;
    c.tral_init_member(&m, min_blocks, &buf);
    assert(c.tral_num_blocks(&m) >= min_blocks);
}

test "mark" {
    var m = initMember(2);
    var adr: u32 = undefined;
    assert(c.tral_mark(&m, 1, &adr));
    assert(0 == adr);
    assert(c.tral_mark(&m, 1, &adr));
    assert(1 == adr);
}

test "clear" {
    var m = initMember(2);
    var adr: u32 = undefined;
    _ = c.tral_mark(&m, 1, &adr);
    const prev_adr = adr;
    c.tral_clear(&m, adr, 1);
    assert(c.tral_mark(&m, 1, &adr));
    assert(adr == prev_adr);
}

test "mark increasing" {
    var m = initMember(8);
    var adr: u32 = undefined;
    _ = c.tral_mark(&m, 1, &adr);
    assert(c.tral_mark(&m, 2, &adr));
    //assert(2 == adr); FIXME
}
