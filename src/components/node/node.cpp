#include "node.hpp"
namespace DiStore::Cluster {
    auto NodeInfo::initialize(std::ifstream &config, NodeInfo *node) -> bool {
        std::string buffer;
        std::regex uri("(\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}):(\\d+)");
        std::regex nid("node(\\d+)");
        node->node_id = node->tcp_port = node->erpc_port = node->roce_port = -1;
        while(std::getline(config, buffer)) {
            std::smatch nid_v;
            if (std::regex_search(buffer, nid_v, nid)) {
                node->node_id = atoi(nid_v[1].str().c_str());
            }

            auto iter = std::sregex_iterator(buffer.begin(), buffer.end(), uri);
            if (iter == std::sregex_iterator()) {
                continue;
            }

            std::smatch match = *iter;
            node->tcp_addr = Cluster::IPV4Addr::make_ipv4_addr(match[1].str()).value();
            node->tcp_port = atoi(match[2].str().c_str());

            ++iter;
            match = *iter;
            node->roce_addr = Cluster::IPV4Addr::make_ipv4_addr(match[1].str()).value();
            node->roce_port = atoi(match[2].str().c_str());

            ++iter;
            match = *iter;
            node->erpc_addr = Cluster::IPV4Addr::make_ipv4_addr(match[1].str()).value();
            node->erpc_port = atoi(match[2].str().c_str());

            node->socket = -1;
        }
        config.clear();
        config.seekg(0);

        if (node->node_id == -1) {
            Debug::error("Failed to parse node id\n");
            return false;
        }

        if (node->tcp_port == -1) {
            Debug::error("Failed to parse tcp info\n");
            return false;
        }

        if (node->roce_port == -1) {
            Debug::error("Failed to parse roce info\n");
            return false;
        }

        if (node->erpc_port == -1) {
            Debug::error("Failed to parse erpc info\n");
            return false;
        }

        return true;
    }

    auto NodeInfo::dump() const noexcept -> void {
        std::cout << ">> Node " << node_id << " is dumping\n";
        std::cout << "---->> TCP: " << tcp_addr.to_uri(tcp_port) << "\n";
        std::cout << "---->> RoCE: " << roce_addr.to_uri(roce_port) << "\n";
        std::cout << "---->> eRPC: " << erpc_addr.to_uri(erpc_port) << "\n";
    }

    auto MemoryNodeInfo::dump() const noexcept -> void {
        NodeInfo::dump();
        std::cout << "---->> Base address is " << base_addr.void_ptr() << "\n";
    }
}
