#include "node.hpp"
namespace DiStore {
    namespace Cluster {
        auto NodeInfo::initialize(std::ifstream &config, NodeInfo *node) -> void {
            std::string buffer;
            std::regex node_info("node(\\d+):\\s*(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
            while(std::getline(config, buffer)) {
                auto iter = std::sregex_iterator(buffer.begin(), buffer.end(), node_info);
                std::smatch match = *iter;
                // can throw exception here
                node->node_id = atoi(match[1].str().c_str());

                ++iter;
                match = *iter;
                node->tcp_addr = Cluster::IPV4Addr::make_ipv4_addr(match[2].str()).value();
                node->tcp_port = atoi(match[3].str().c_str());

                ++iter;
                match = *iter;
                node->roce_addr = Cluster::IPV4Addr::make_ipv4_addr(match[2].str()).value();
                node->roce_port = atoi(match[3].str().c_str());

                ++iter;
                match = *iter;
                node->erpc_addr = Cluster::IPV4Addr::make_ipv4_addr(match[2].str()).value();
                node->erpc_port = atoi(match[3].str().c_str());

                node->socket = -1;
            }
            config.clear();
            config.seekg(0);
        }

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
