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

    sleep(10);
}

auto launch_memory(const std::string &config) -> void {
    auto node = Cluster::MemoryNode::make_memory_node(config);

    if (node == nullptr) {
        Debug::error("Wow you can do a really bad job\n");
        return;
    }

    node->launch_tcp_thread().value().detach();
    node->launch_rdma_thread().value().detach();
    node->launch_erpc_thread().value().detach();

    sleep(10);
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
