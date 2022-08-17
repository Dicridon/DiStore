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

    auto a = allocator.allocate(128);
    std::cout << ">> Allocated at " << a.void_ptr() << "\n";
    allocator.dump();    
    a = allocator.allocate(128);
    std::cout << ">> Allocated at " << a.void_ptr() << "\n";
    allocator.dump();    
    a = allocator.allocate(128);
    std::cout << ">> Allocated at " << a.void_ptr() << "\n";
    allocator.dump();    
    a = allocator.allocate(128);
    std::cout << ">> Allocated at " << a.void_ptr() << "\n";
    allocator.dump();    


    a = allocator.allocate(63);
    std::cout << ">> Allocated at " << a.void_ptr() << "\n";
    allocator.dump();    
}
