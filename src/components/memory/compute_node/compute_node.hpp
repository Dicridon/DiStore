#ifndef __DISTORE__MEMORY__COMPUTE_NODE__COMPUTE_NODE__
#define __DISTORE__MEMORY__COMPUTE_NODE__COMPUTE_NODE__
#include "../../node/node.hpp"
#include "../memory.hpp"
#include "../remote_memory/remote_memory.hpp"
#include "../memory_node/memory_node.hpp"

#include <unordered_map>
#include <thread>

namespace DiStore {
    namespace Memory {
        namespace Constants {
            constexpr size_t MEMORY_PAGE_SIZE = 4096;
        }

        struct PageDescriptor {
            byte_t empty_slots;
            byte_t allocation_class : 5;
            byte_t synced : 3;
            byte_t offset;

            auto clear() -> void {
                empty_slots = 0;
                allocation_class = 0;
                synced = 0;
                offset = 0;
            }
        } __attribute__((packed));

        struct PageMirror {
            PageDescriptor desc;
            uint64_t page_id : 40;
        } __attribute__((packed));

        struct PageGroup {
            PageMirror pages[8];
        };

        struct SegmentTracker {
            RemotePointer seg;
            size_t offset;

            auto assign_new_seg(RemotePointer seg) -> void {
                this->seg = seg;
                offset = 0;
            }

            auto available_for(size_t size) -> bool {
                if (offset + size >= Constants::SEGMENT_SIZE) {
                    return false;
                }

                return true;
            }

            SegmentTracker() : seg(nullptr), offset(0) {};
            SegmentTracker(const SegmentTracker &) = delete;
            SegmentTracker(SegmentTracker &&) = delete;
            auto operator=(const SegmentTracker &) = delete;
            auto operator=(SegmentTracker &&) = delete;
        };

        enum AllocationClass {
            Chunk32 = 1,
            Chunk64 = 2,
            Chunk128 = 3,
            Chunk256 = 4,
            Chunk512 = 5,
            Chunk1024 = 6,
            Chunk4096 = 7,
        };

        class ComputeNodeAllocator {
        public:
            auto apply_for_memory(RemotePointer seg) -> void {
                tracker.assign_new_seg(seg);
            }

            auto return_memory() -> byte_ptr_t {
                
            }

            // allocate will NOT fetch a new segment if current segment is used up
            // ComputeNode should fetch
            auto allocate(size_t sz) -> byte_ptr_t;

            // 
            auto free(RemotePointer ptr) -> void;

            
            ComputeNodeAllocator() = default;
            ComputeNodeAllocator(const ComputeNodeAllocator &) = delete;
            ComputeNodeAllocator(ComputeNodeAllocator &&) = delete;
            auto operator=(const ComputeNodeAllocator &) = delete;
            auto operator=(ComputeNodeAllocator &&) = delete;
        private:
            SegmentTracker tracker;
            std::unordered_map<std::thread::id, PageGroup *> thread_info;
        };

        
        class RemoteMemoryManager {
        public:
            RemoteMemoryManager() = default;

            // parse config file to find all memory nodes
            auto parse_config_file(const std::string &config) -> bool;

            // connect all memory nodes presented in the config file
            auto connect_memory_nodes() -> bool;

            auto get_base_addr() -> RemotePointer;

            auto offer_remote_segment() -> RemotePointer;
            auto recycle_remote_segment(RemotePointer segment) -> bool;

            
            RemoteMemoryManager(const RemoteMemoryManager &) = delete;
            RemoteMemoryManager(RemoteMemoryManager &&) = delete;
            auto operator=(const RemoteMemoryManager &) = delete;
            auto operator=(RemoteMemoryManager &&) = delete;
        private:
            std::vector<Cluster::MemoryNodeInfo> memory_nodes;
        };
    }
}
#endif
