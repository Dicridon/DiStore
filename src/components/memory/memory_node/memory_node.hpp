#ifndef __DISTORE__MEMORY__MEMORY_NODE__MEMORY_NODE__
#define __DISTORE__MEMORY__MEMORY_NODE__MEMORY_NODE__
#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"
namespace DiStore {
    namespace Memory {
        class MemoryNodeAllocator {
        public:
            static auto make_allocator(byte_ptr_t region, size_t memory_size) -> MemoryNodeAllocator * {
                auto allocator = reinterpret_cast<MemoryNodeAllocator *>(region);
                auto bitmap = reinterpret_cast<Bitmap *>(region);
                
                auto num_segments = memory_size / Constants::SEGMENT_SIZE;
                bitmap = Bitmap::make_bitmap(region, num_segments);
                return allocator;
            }

            auto allocate() -> byte_ptr_t {
                auto no = bitmap.get_empty();
                if (!no.has_value()) {
                    return nullptr;
                }

                return reinterpret_cast<byte_ptr_t>(this) + 4096 + no.value() * Constants::SEGMENT_SIZE;
            }
            
            auto deallocate(byte_ptr_t segment) -> void {
                auto pos = (segment - reinterpret_cast<byte_ptr_t>(this) - 4096) / Constants::SEGMENT_SIZE;
                bitmap.unset(pos);
            }


            MemoryNodeAllocator() = delete;
            MemoryNodeAllocator(const MemoryNodeAllocator &) = delete;
            MemoryNodeAllocator(MemoryNodeAllocator &&) = delete;
            auto operator=(const MemoryNodeAllocator &) = delete;
            auto operator=(MemoryNodeAllocator &&) = delete;
            
        private:
            Bitmap bitmap;
        };
    }
}
#endif
