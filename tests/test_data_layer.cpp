#include "data_layer/data_layer.hpp"
using namespace DiStore::DataLayer; 

auto main() -> int {
    LinkedNode10 node;

    auto start = 100;
    for (int i = 0; i < 10; i++) {
        auto key = std::to_string(start + i);
        auto value = key;

        node.store(key, value);
        auto v = node.find(key);
        std::cout << v.value() << "\n";
    }
}
