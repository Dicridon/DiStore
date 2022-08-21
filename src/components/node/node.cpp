#include "node.hpp"
namespace DiStore {
    namespace Cluster {
        auto NodeInfo::dump() const noexcept -> void {
            std::cout << ">> Node " << node_id << " is dumping\n";
            std::cout << "---->> TCP: " << tcp_addr.to_string() + std::to_string(tcp_port) << "\n";
            std::cout << "---->> RoCE: " << roce_addr.to_string() + std::to_string(roce_port) << "\n";
            std::cout << "---->> eRPC: " << erpc_addr.to_string() + std::to_string(erpc_port) << "\n";
        }

        auto MemoryNodeInfo::dump() const noexcept -> void {
            NodeInfo::dump();
            std::cout << "---->> Base address is " << base_addr.void_ptr() << "\n";
        }
    }
}
