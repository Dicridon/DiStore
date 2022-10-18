#include "node/memory_node/memory_node.hpp"
#include "node/compute_node/compute_node.hpp"
#include "cmd_parser/cmd_parser.hpp"

using namespace CmdParser;
using namespace DiStore;

auto launch_compute(const std::string &config, const std::string &memory_nodes, int threads) -> void {
    auto node = Cluster::ComputeNode::make_compute_node(config, memory_nodes);

    if (node == nullptr) {
        Debug::error("Wow you can do a really bad job\n");
        return;
    }

    if (!node->register_thread()) {
        Debug::error("Failed to register a thread\n");
        return;
    }

    std::cout << "Populating\n";
    for (size_t i = 0; i < 100; i++) {
        auto k = std::to_string(1000000 + i);
        k.append(DataLayer::Constants::KEYLEN - k.size(), '0');
        auto v = std::to_string(2000000 + i);

        node->put(k, v);
        auto r = node->get(k);
        std::cout << r.value() << "\n";
    }

    std::cout << "Double checking\n";
    for (size_t i = 0; i < 100; i++) {
        auto k = std::to_string(1000000 + i);
        k.append(DataLayer::Constants::KEYLEN - k.size(), '0');
        auto r = node->get(k);
        if (!r.has_value()) {
            std::cout << k << " is missing\n";
            std::cout << "Double checking failed\n";
            break;
        }
    }

    std::cout << "Updating\n";
    for (size_t i = 0; i < 100; i++) {
        auto k = std::to_string(1000000 + i);
        k.append(DataLayer::Constants::KEYLEN - k.size(), '0');
        if (!node->update(k, k)) {
            std::cout << "Updaing " << k << " failed\n";
            break;
        }
    }

    while (true)
        ;
}

auto launch_memory(const std::string &config) -> void {
    auto node = Cluster::MemoryNode::make_memory_node(config);

    if (node == nullptr) {
        Debug::error("Wow you can do a really bad job\n");
        return;
    }

    node->launch_erpc_thread().value().detach();
    node->launch_tcp_thread().value().detach();
    node->launch_rdma_thread().value().detach();

    while (true)
        ;
}

auto main(int argc, char *argv[]) -> int {
    Parser parser;
    parser.add_option("--type", "-t", "compute");
    parser.add_option("--config", "-c");
    parser.add_option("--memory_nodes", "-m");
    parser.add_option<int>("--threads", "-T", 1);

    parser.parse(argc, argv);

    auto type = parser.get_as<std::string>("--type");
    auto config = parser.get_as<std::string>("--config");
    auto memory_nodes = parser.get_as<std::string>("--memory_nodes");
    auto threads = parser.get_as<int>("--threads");

    if (type == "compute") {
        if (!config.has_value()) {
            Debug::error("Please offer a configuration file to configure current node\n");
            return -1;
        }

        if (!memory_nodes.has_value()) {
            Debug::error("Please offer a configuration file about the memory nodes\n");
            return -1;
        }

        launch_compute(config.value(), memory_nodes.value(), threads.value());
    } else if (type == "memory") {
        if (!config.has_value()) {
            Debug::error("Please offer a configuration file to configure current node\n");
            return -1;
        }

        launch_memory(config.value());
    } else {
        Debug::error("Unknown node type %s\n", type.value().c_str());
        return -1;
    }
}
