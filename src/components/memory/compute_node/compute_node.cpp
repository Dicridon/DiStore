#include "compute_node.hpp"

namespace DiStore {
    namespace Memory {
        auto PageMirror::allocate() -> RemotePointer {
            if (!available())
                return nullptr;

            auto base = page_base;
            --desc.empty_slots;
            return base.offset((desc.offset++) * allocation_class_size_map[desc.allocation_class]);
        }

        auto PageMirror::available() -> bool {
            auto total = Constants::MEMORY_PAGE_SIZE / allocation_class_size_map[desc.allocation_class];
            if (desc.offset >= total) {
                return false;
            }
            return true;
        }

        auto PageMirror::free(RemotePointer ptr) -> bool {
            if (page_base != ptr.page())
                return false;

            // TODO: add reclaimation update to memory node
            // Alternatively, we can trigger gloabl GC on memory node to recycle
            return ++desc.empty_slots;
        }

        // think more about the detail
        auto PageGroup::allocate(AllocationClass ac) -> RemotePointer {
            if (!available(ac)) {
                return nullptr;
            }

            for (auto &p : pages) {
                if (p.desc.allocation_class == ac) {
                    return p.allocate();
                } else if (p.desc.allocation_class == AllocationClass::ChunkUnknown) {
                    p.desc.allocation_class = ac;
                    p.desc.empty_slots = (Constants::MEMORY_PAGE_SIZE / allocation_class_size_map[ac]);
                    p.desc.offset = 0;
                    p.desc.synced = 0;
                    return p.allocate();
                }
            }

            return nullptr;
        }

        auto PageGroup::available(AllocationClass ac) -> bool {
            for (auto &p : pages) {
                if (p.desc.allocation_class == ac) {
                    return p.available();
                }
            }

            return false;
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
                if (refill_single_page(id, ac) == false)
                    return nullptr;
            }

            return group->second->allocate(ac);
        }

        auto ComputeNodeAllocator::free(RemotePointer chunk) -> void {
            auto page = chunk.page();

            auto current = tracker.current;
            if (auto p = current->mirrors.find(page); p != current->mirrors.end()) {
                p->second->free(chunk);
            }

            for (auto &k : tracker.segments) {
                auto &mirror = k.second->mirrors;
                if (auto p = mirror.find(page); p != mirror.end()) {
                    p->second->free(page);
                }
            }
        }

        auto ComputeNodeAllocator::refill(const std::thread::id &id) -> bool {

        }

        auto ComputeNodeAllocator::refill_single_page(const std::thread::id &id, AllocationClass ac) -> bool {

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
