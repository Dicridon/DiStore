#include <iostream>

#include "memory/compute_node/compute_node.hpp"

using namespace DiStore;
using namespace DiStore::Memory;

auto main() -> int {
    ComputeNodeAllocator allocator;

    auto buffer = new byte_t [1 << 20];
    auto segment = RemotePointer::make_remote_pointer(0, buffer);
    auto base = segment;

    allocator.apply_for_memory(segment, base);
    allocator.dump();

    for (size_t i = 1; i <= 4096; i++) {
        std::cout << i << ": " << dump_allocation_class(allocator.get_class(i)) << "\n";
    }
}
