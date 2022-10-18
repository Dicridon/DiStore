#include "memory/compute_node/compute_node.hpp"

using namespace DiStore::Memory;

auto main() -> int {
    ComputeNodeAllocator allocator;
    allocator.allocate(1064);
    return 0;
}
