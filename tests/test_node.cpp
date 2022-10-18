#include "node/node.hpp"

int main() {
    DiStore::Cluster::NodeInfo node;
    std::ifstream file("fdsa");
    node.initialize(file);
}
