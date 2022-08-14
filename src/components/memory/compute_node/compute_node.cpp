#include "compute_node.hpp"

namespace DiStore {
    namespace Memory {

        // think more about the detail
        auto PageGroup::allocate(AllocationClass ac) -> RemotePointer {
            auto slot_size = 16 * (1 << ac);
            auto total = (Memory::Constants::MEMORY_PAGE_SIZE - sizeof(PageDescriptor)) / slot_size;

            for (int i = 0; i < 8; i++) {
                if (pages->desc.allocation_class == ac) {
                    
                }
            }
        }

        auto PageGroup::available(AllocationClass ac) -> bool {
            
        }

        auto ComputeNodeAllocator::allocate(size_t sz) -> RemotePointer {
            if (sz == 0) {
                throw new std::runtime_error("Received 0 size in " + std::string(__FUNCTION__) + "\n");
            }
            
            auto id = std::this_thread::get_id();

            auto group = thread_info.find(id);
            if (group == thread_info.end()) {
                if (refill(id) == false)
                    return nullptr;
            }

            auto ac = get_class(sz);
            if (!group->second->available(ac)) {
                if (refill_single_page(id) == false)
                    return nullptr;
            }

            return group->second->allocate(ac);
        }

        auto ComputeNodeAllocator::free(RemotePointer chunk) -> void {
            
        }

        auto ComputeNodeAllocator::refill(const std::thread::id &id) -> bool {
            
        }

        auto ComputeNodeAllocator::refill_single_page(const std::thread::id &id) -> bool {
            
        }
        
        auto ComputeNodeAllocator::return_memory() -> bool {
            
        }

        auto RemoteMemoryManager::parse_config_file(const std::string &config) -> bool {
            
        }

        auto RemoteMemoryManager::connect_memory_nodes() -> bool {
            
        }

        auto RemoteMemoryManager::get_base_addr(int node_id) -> RemotePointer {
            
        }

        auto RemoteMemoryManager::offer_remote_segment() -> RemotePointer {
            
        }

        auto RemoteMemoryManager::recycle_remote_segment(RemotePointer segment) -> bool {
            
        }
    }
}
