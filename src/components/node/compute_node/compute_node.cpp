#include "compute_node.hpp"
namespace DiStore {
    namespace Cluster {
        auto ComputeNode::initialize(const std::string &compute_config, const std::string &memory_config)
            -> bool
        {
            std::ifstream file(compute_config);
            if (!file.is_open()) {
                Debug::error("Faild to open config file %s\n", compute_config.c_str());
                return false;
            }
            
            NodeInfo::initialize(file, &self_info);

            if (!initialize_erpc()) {
                return false;
            }

            if (!initialize_rdma_dev(file)) {
                return false;
            }
        
            remote_memory_allocator.parse_config_file(memory_config);
            if (!remote_memory_allocator.connect_memory_nodes()) {
                return false;
            }
            remote_memory_allocator.rpc_ctx = &compute_ctx;

            auto self = self_info.tcp_addr.to_string() + std::to_string(self_info.tcp_port);
            Debug::info("Compute node %s is intialized\n", self.c_str());
            return true;
        }
    }
}
