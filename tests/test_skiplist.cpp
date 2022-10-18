#include "search_layer/search_layer.hpp"

#include <iostream>

using namespace DiStore::SearchLayer;
using namespace DiStore::DataLayer;
auto main() -> int {
    auto slist = SkipList::make_skiplist();

    auto r = RemotePointer::make_remote_pointer(1, 12354);

    auto start = 10000000UL;

    std::cout << ">>>>>>>>>>>>>>>>>>> Inserting\n";
    for (int i = 0; i < 1000; i++) {
        auto k = std::to_string(start + i * 10);
        slist->insert(k, r, LinkedNodeType::Type10);
        slist->dump();
    }

    std::cout << ">>>>>>>>>>>>>>>>>>> Fuzzy searching\n";

    for (int i = 100; i > 0; i--) {
        auto k = std::to_string(start + i);
        auto node = slist->fuzzy_search(k);
        RemotePointer r;
        if (node != nullptr)
            r = node->data_node;
        std::cout << ">> Fuzzy searching " << k << ", got " << r.void_ptr() << "\n";
    }

    for (int i = 10; i > 0; i--) {
        auto k = std::to_string(start + i * 10);
        slist->remove(k);
        std::cout << ">> Remove dumping\n";
        slist->dump();

        auto node = slist->search(k);
        RemotePointer r;
        if (node != nullptr)
            r = node->data_node;
        std::cout << ">> Fuzzy searching " << k << ", got " << r.void_ptr() << "\n";
    }
    return 0;
}
