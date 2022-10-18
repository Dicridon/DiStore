#include "tbb/concurrent_queue.h"
#include "data_layer/data_layer.hpp"
#include "search_layer/search_layer.hpp"
#include "node/compute_node/compute_node.hpp"

using namespace DiStore;
using namespace DiStore::SearchLayer;
using namespace DiStore::DataLayer;
using namespace DiStore::Memory;
using namespace DiStore::Cluster;

tbb::concurrent_queue<CalibrateContext *> update_queue;

auto async_update(SkipListNode *data_node, const std::string &anchor,
                               LinkedNodeType t, RemotePointer r)
    -> void
{
    auto [new_node, level] = SkipList::make_new_node(anchor, r, t);
    std::cout << "Asyncing new node of " << anchor << " with level " << level << "\n";
    new_node->forwards[0] = data_node->forwards[0];
    data_node->forwards[0] = new_node;
    new_node->backward = data_node;

    auto req = new CalibrateContext;
    req->level = level;
    req->new_node = new_node;

    update_queue.push(req);
}


auto main() -> int {
    DiStore::SearchLayer::SkipList slist;

    bool run = true;

    std::thread([&] {
        while (run) {
            CalibrateContext *cal;
            if (update_queue.try_pop(cal)) {
                slist.calibrate(cal->new_node, cal->level);
            }
        }
    }).detach();


    auto start = 100000;
    slist.insert(std::to_string(start + 1), RemotePointer::make_remote_pointer(0, 1),
                 LinkedNodeType::Type10);

    for (int i = 1; i < 100; i++) {
        std::cout << "Current list\n";
        slist.dump();
        auto node = slist.search(std::to_string(start + i));

        async_update(node, std::to_string(start + i + 1),
                     LinkedNodeType::Type10,
                     RemotePointer::make_remote_pointer(0, start + i + 1));

        slist.dump();

        // if (i == 11) while(true);
    }

    run = false;
    return 0;
}
