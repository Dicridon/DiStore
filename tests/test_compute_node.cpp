#include "node/compute_node/compute_node.hpp"

auto main() -> int {
    auto cn = DiStore::Cluster::ComputeNode::make_compute_node("compute_node.config", "memory_nodes.config");
    cn->report_cluster_info();
    return 0;
}
