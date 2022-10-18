#include "memory/compute_node/compute_node.hpp"

#include <iostream>

using namespace DiStore;
using namespace DiStore::Memory;
auto main() -> int {
    auto remote = RemoteMemoryManager();

    remote.parse_config_file("./memory_nodes.config");
    remote.dump();
}
