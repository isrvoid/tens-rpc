#include "tree_allocator.h"

#include <assert.h>
#include <string.h>

// anything with 'leaves' in the name refers to the continuous block bitmap
// a leaf is an uint32_t bit-mapping 32 blocks
// top tree node has at least 2 branches, everything else has NUM_BRANCHES branches
#define NUM_BRANCHES_LOG2 5
#define NUM_BRANCHES (1 << NUM_BRANCHES_LOG2)

// implementation assumes at least 32-bit ints for bit shifting stuff
static_assert(sizeof 1 >= 4, "int size < 32-bit");
static_assert(TRAL_MARK_MAX_BLOCKS == 1 << NUM_BRANCHES_LOG2, "MARK_MAX_BLOCKS out of sync");

static int treeHeightLeafless(size_t min_blocks) {
    int h = 0;
    size_t v = min_blocks;
    for (; v > NUM_BRANCHES; ++h)
        v >>= NUM_BRANCHES_LOG2;

    assert(h > 0);
    return h;
}

static int numTopNodeBranches(size_t min_blocks) {
    const int h = treeHeightLeafless(min_blocks);
    const uint32_t num_top_branch_blocks = 1u << NUM_BRANCHES_LOG2 * h;
    return min_blocks / num_top_branch_blocks + !!(min_blocks % num_top_branch_blocks);
}

static uint32_t numLeaves(size_t min_blocks) {
    const int top_branches = numTopNodeBranches(min_blocks);
    const int h = treeHeightLeafless(min_blocks);
    return top_branches << NUM_BRANCHES_LOG2 * (h - 1);
}

static uint32_t numTreeNodes(uint32_t min_blocks) {
    const int h = treeHeightLeafless(min_blocks);
    uint32_t num_nodes_at_current_depth = numTopNodeBranches(min_blocks);
    uint32_t res = 1; // top node
    for (int i = 1; i < h; ++i, num_nodes_at_current_depth <<= NUM_BRANCHES_LOG2)
        res += num_nodes_at_current_depth;

    return res;
}

static size_t checkMinBlocks(size_t min_blocks) {
    assert(min_blocks > 0 && min_blocks <= UINT64_C(1) << 32);
    const size_t lower_cap = NUM_BRANCHES * 2; // ensures tree height > 0
    return min_blocks < lower_cap ? lower_cap : min_blocks;
}

static uint32_t requiredBufferSize(uint32_t num_leaves, uint32_t num_tree_nodes) {
    const int num_trees = NUM_BRANCHES_LOG2 + 1;
    return (num_leaves + num_tree_nodes * num_trees) * 4;
}

uint32_t tral_required_member_buffer_size(size_t min_blocks) {
    min_blocks = checkMinBlocks(min_blocks);
    const uint32_t num_leaves = numLeaves(min_blocks);
    const uint32_t num_tree_nodes = numTreeNodes(min_blocks);
    return requiredBufferSize(num_leaves, num_tree_nodes);
}

void tral_init_member(tral_member_t* m, size_t min_blocks, void* buf) {
    min_blocks = checkMinBlocks(min_blocks);
    memset(m, 0, sizeof(tral_member_t));
    m->tree_height = treeHeightLeafless(min_blocks);
    m->num_top_branches = numTopNodeBranches(min_blocks);
    m->num_leaves = numLeaves(min_blocks);
    m->tree_stride = numTreeNodes(min_blocks);
    const uint32_t buf_size = requiredBufferSize(m->num_leaves, m->tree_stride);
    memset(buf, 0, buf_size);
    m->buf = buf;
}

size_t tral_num_blocks(const tral_member_t* m) {
    assert(sizeof(size_t) > 4 || m->num_leaves < 1 << (32 - NUM_BRANCHES_LOG2));
    return (size_t)m->num_leaves << NUM_BRANCHES_LOG2;
}

bool tral_mark(tral_member_t* m, uint32_t size, uint32_t* adr_out) {
    assert(size && size <= TRAL_MARK_MAX_BLOCKS);
    // FIXME dummy
    uint8_t* p = (uint8_t*)m->buf;
    for (int i = 0; i < 8; ++i)
        if (!p[i]) {
            p[i] = 1;
            *adr_out = i;
            return true;
        }
    return false;
}

void tral_clear(tral_member_t* m, uint32_t adr, uint32_t size) {
    assert(size && size <= TRAL_MARK_MAX_BLOCKS);
    // FIXME dummy
    uint8_t* p = (uint8_t*)m->buf;
    p[adr] = 0;
}
