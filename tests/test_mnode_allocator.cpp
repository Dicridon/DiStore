#include "memory/memory_node/memory_node.hpp"

using namespace DiStore::Memory;
auto main() -> int {
    auto region = new byte_t[1024 * 1024];

    auto allocator = MemoryNodeAllocator::make_allocator(region, 1024 * 1024);

    for (int i = 0; i < 64; i++) {
        auto chunk = allocator->allocate();
        std::cout << "got " << (void *)chunk << "\n";
    }
    return 0;
}
