#ifndef __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#define __DISTORE__NODE__COMPUTE_NODE__COMPUTE_NODE__
#include "../node.hpp"
#include "../../memory/memory.hpp"
#include "../../memory/compute_node/compute_node.hpp"
#include "../../kv/kv.hpp"


namespace DiStore {
    namespace Cluster {
        class ComputeNode {
        public:
            ComputeNode(const std::string &config);

            auto connect_memory_nodes() -> bool;

            auto put(const std::string &key, const std::string &value) -> bool;
            auto get(const std::string &key) -> std::string;
            auto update(const std::string &key, const std::string &value) -> bool;
            auto remove(const std::string &key) -> bool;
            auto scan(const std::string &key, size_t count) -> std::vector<Value>;

            ComputeNode(const ComputeNode &) = delete;
            ComputeNode(ComputeNode &&) = delete;
            auto operator=(const ComputeNode &) = delete;
            auto operator=(ComputeNode &&) = delete;
        private:
            ComputeNodeInfo info;
            Memory::ComputeNodeAllocator allocator;
            Memory::RemoteMemoryManager remote_memory_allocator;
            std::vector<MemoryNodeInfo> memory_nodes;
        };
    }
}
#endif
