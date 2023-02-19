#include "tree_allocator.h"

#include <assert.h>
#include <string.h>

// anything with 'leaves' in the name refers to the continuous block bitmap
// a leaf is an uint32_t bit-mapping 32 blocks
// top tree node has at least 2 branches, everything else has NUM_BRANCHES branches
#define NUM_BRANCHES_LOG2 5
#define NUM_BRANCHES (1 << NUM_BRANCHES_LOG2)
#define BRANCH_INDEX_MASK (NUM_BRANCHES - 1)
#define NUM_TREES (NUM_BRANCHES_LOG2 + 1)
#define UPPER_LOG2_SMALL(x) ((x > 1) + (x > 2) + (x > 4) + (x > 8) + (x > 16))

// implementation assumes at least 32-bit ints for bit shifting stuff
static_assert(sizeof 1 >= 4, "int size < 32-bit");
static_assert(TRAL_MARK_MAX_BLOCKS == 1 << NUM_BRANCHES_LOG2, "MARK_MAX_BLOCKS out of sync");

// The trees share leaves that are stored separately.
// Because of this, the trees are 1 shorter than a single tree would be.
static int treeHeight(size_t min_blocks) {
    int h = 0;
    size_t v = min_blocks;
    for (; v > NUM_BRANCHES; ++h)
        v >>= NUM_BRANCHES_LOG2;

    assert(h > 0);
    return h;
}

static int numTopNodeBranches(size_t min_blocks) {
    const int h = treeHeight(min_blocks);
    const uint32_t num_top_branch_blocks = 1u << NUM_BRANCHES_LOG2 * h;
    return min_blocks / num_top_branch_blocks + !!(min_blocks % num_top_branch_blocks);
}

static uint32_t numLeaves(size_t min_blocks) {
    const uint32_t num_top_branches = numTopNodeBranches(min_blocks);
    const int h = treeHeight(min_blocks);
    return num_top_branches << NUM_BRANCHES_LOG2 * (h - 1);
}

static uint32_t numTreeNodes(uint32_t min_blocks) {
    const int h = treeHeight(min_blocks);
    uint32_t row_width = numTopNodeBranches(min_blocks);
    uint32_t res = 1; // top node
    for (int i = 1; i < h; ++i, row_width <<= NUM_BRANCHES_LOG2)
        res += row_width;
    return res;
}

static tral_index_slice_t bottomRow(int num_top_branches, int tree_height) {
    if (tree_height == 1)
        return (tral_index_slice_t){ 0, 1 };

    // starting with second row
    uint32_t offset = 1;
    uint32_t row_width = num_top_branches;
    for (int row_i = 1; ; offset += row_width, row_width <<= NUM_BRANCHES_LOG2)
        if (++row_i == tree_height)
            return (tral_index_slice_t){ offset, row_width };
}

static size_t checkMinBlocks(size_t min_blocks) {
    assert(min_blocks > 0 && min_blocks <= UINT64_C(1) << 32);
    const size_t lower_cap = NUM_BRANCHES * 2; // ensures tree height > 0
    return min_blocks < lower_cap ? lower_cap : min_blocks;
}

static uint32_t requiredBufferSize(uint32_t num_leaves, uint32_t num_tree_nodes) {
    return (num_leaves + num_tree_nodes * NUM_TREES) * 4;
}

uint32_t tral_required_member_buffer_size(size_t min_blocks) {
    min_blocks = checkMinBlocks(min_blocks);
    const uint32_t num_leaves = numLeaves(min_blocks);
    const uint32_t num_tree_nodes = numTreeNodes(min_blocks);
    return requiredBufferSize(num_leaves, num_tree_nodes);
}

static void initTopNodes(tral_member_t* m) {
    uint32_t* p = (uint32_t*)m->buf + m->num_leaves;
    uint32_t* const end = p + m->tree_stride * NUM_TREES;
    const uint32_t non_existent_marked = ~((1u << m->num_top_branches) - 1);
    for (; p < end; p += m->tree_stride)
        *p = non_existent_marked;
}

void tral_init_member(tral_member_t* m, size_t min_blocks, void* buf) {
    min_blocks = checkMinBlocks(min_blocks);
    memset(m, 0, sizeof(tral_member_t));
    m->tree_height = treeHeight(min_blocks);
    m->num_top_branches = numTopNodeBranches(min_blocks);
    m->num_leaves = numLeaves(min_blocks);
    m->tree_stride = numTreeNodes(min_blocks);
    m->bottom_row = bottomRow(m->num_top_branches, m->tree_height);
    const uint32_t buf_size = requiredBufferSize(m->num_leaves, m->tree_stride);
    memset(buf, 0, buf_size);
    m->buf = buf;
    initTopNodes(m);
}

size_t tral_num_blocks(const tral_member_t* m) {
    assert(sizeof(size_t) > 4 || m->num_leaves < 1 << (32 - NUM_BRANCHES_LOG2));
    return (size_t)m->num_leaves << NUM_BRANCHES_LOG2;
}

static inline int countTrailingZeros(uint32_t x) {
    x = x & -x;
    return !!(x & 0xffff0000) << 4 | !!(x & 0xff00ff00) << 3 | !!(x & 0xf0f0f0f0) << 2 | !!(x & 0xcccccccc) << 1 | !!(x & 0xaaaaaaaa);
}

static inline int indexOfFirstZero(uint32_t x) {
    return countTrailingZeros(~x);
}

static inline uint32_t leafIndex(uint32_t* const top_node, int num_top_branches, int tree_height) {
    uint32_t node_i = indexOfFirstZero(*top_node);
    const uint32_t* row = top_node + 1;
    uint32_t row_width = num_top_branches;
    for (int i = 1; i < tree_height; ++i, row += row_width, row_width <<= NUM_BRANCHES_LOG2) {
        const int branch_i = indexOfFirstZero(row[node_i]);
        node_i = (node_i << NUM_BRANCHES_LOG2) + branch_i;
    }
    return node_i;
}

static inline int leafBlocksOffset(uint32_t x, int num_blocks_log2) {
    switch (num_blocks_log2) {
        case 5:
            return 0;
        case 4:
            return !!(x & 0xffff) << 4;
        case 3:
            return (!!(x & 0xff00) & !!(x & 0xff)) << 4 | (!!(x & 0xff0000) & !!(x & 0xff)) << 3;
        case 2:
            x = x >> 1 | x | 0xaaaaaaaa;
            x = x >> 2 | x | 0xeeeeeeee;
            x = ~x & -~x;
            return !!(x & 0xffff0000) << 4 | !!(x & 0xff00ff00) << 3 | !!(x & 0xf0f0f0f0) << 2;
        case 1:
            x = x >> 1 | x | 0xaaaaaaaa;
            // fallthrough
        case 0:
            return indexOfFirstZero(x);
    }
    assert(0);
}

static inline int leafHasSpaceEnd(uint32_t leaf) {
    uint32_t free_blocks = ~leaf;
    int i = !!free_blocks + !leaf;
    free_blocks = free_blocks >> 1 & free_blocks & 0x55555555;
    i += !!free_blocks;
    free_blocks = free_blocks >> 2 & free_blocks & 0x11111111;
    i += !!free_blocks;
    free_blocks = free_blocks >> 4 & free_blocks & 0x01010101;
    i += !!free_blocks;
    free_blocks = free_blocks >> 8 & free_blocks & 0x00010001;
    i += !!free_blocks;
    return i;
}

static inline void updateTreeLeafFull(uint32_t* bottom_row, uint32_t leaf_i, uint32_t bottom_row_width, int tree_height) {
    uint32_t row_width = bottom_row_width;
    uint32_t* row = bottom_row;
    int branch_i = leaf_i & BRANCH_INDEX_MASK;
    uint32_t node_i = leaf_i >> NUM_BRANCHES_LOG2;
    for (int row_i = tree_height - 1; ; --row_i, branch_i = node_i & BRANCH_INDEX_MASK, node_i >>= NUM_BRANCHES_LOG2, row_width >>= NUM_BRANCHES_LOG2, row -= row_width) {
        uint32_t* const node = row + node_i;
        *node |= 1u << branch_i;
        const bool node_has_space_left = ~*node;
        if (row_i == 0 || node_has_space_left) return;
    }
}

bool tral_mark(tral_member_t* m, uint32_t num_blocks, uint32_t* adr_out) {
    assert(num_blocks && num_blocks <= TRAL_MARK_MAX_BLOCKS);
    const int num_blocks_log2 = UPPER_LOG2_SMALL(num_blocks);
    uint32_t* const leaves = (uint32_t*)m->buf;
    uint32_t* const tree0 = leaves + m->num_leaves;
    uint32_t* const tree = tree0 + num_blocks_log2 * m->tree_stride;
    if (*tree == UINT32_MAX) return false;

    const uint32_t leaf_i = leafIndex(tree, m->num_top_branches, m->tree_height);
    uint32_t* const leaf = leaves + leaf_i;
    const int blocks_offset = leafBlocksOffset(*leaf, num_blocks_log2);
    const uint32_t mark_width_mask = (1u << (1 << num_blocks_log2)) - 1;
    *leaf |= mark_width_mask << blocks_offset;
    *adr_out = (leaf_i << NUM_BRANCHES_LOG2) + blocks_offset;

    const int update_start_i = leafHasSpaceEnd(*leaf);
    uint32_t* bottom_row_it = tree0 + m->tree_stride * update_start_i + m->bottom_row.i;
    for (int i = update_start_i; i < NUM_TREES; ++i, bottom_row_it += m->tree_stride)
        updateTreeLeafFull(bottom_row_it, leaf_i, m->bottom_row.len, m->tree_height);
    return true;
}

void tral_clear(tral_member_t* m, uint32_t adr, uint32_t num_blocks) {
    assert(num_blocks && num_blocks <= TRAL_MARK_MAX_BLOCKS);
    assert(adr <= (m->num_leaves << NUM_BRANCHES_LOG2) - 1);
    const int num_blocks_log2 = UPPER_LOG2_SMALL(num_blocks);
    // FIXME dummy
    uint8_t* p = (uint8_t*)m->buf;
    p[adr] = 0;
}
