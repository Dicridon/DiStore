#ifndef __DISTORE__SEARCH_LAYER__SEARCH_LAYER__
#define __DISTORE__SEARCH_LAYER__SEARCH_LAYER__
#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"

namespace DiStore::SearchLayer {
    using namespace Memory;
    namespace Constants {
        static constexpr size_t MAX_LEVEL = 16;
    }

    struct SkipListNode {
        std::string anchor;
        RemotePointer data_node;
        SkipListNode *next;

        SkipListNode *forwards[];

        static auto make_skip_node(size_t level, const std::string &k,
                                   SkipListNode *n = nullptr)
            -> std::unique_ptr<SkipListNode>
        {
            auto buffer = new byte_t[sizeof(SkipListNode) + level * sizeof(SkipListNode *)];
            auto anchor = new (buffer) std::string;
            anchor->assign(k);
            auto ret = reinterpret_cast<SkipListNode *>(buffer);
            ret->next = n;
            for (size_t i = 0; i < level; i++) {
                ret->forwards[i] = nullptr;
            }

            return std::unique_ptr<SkipListNode>(ret);
        }
    };
    // This skip list is not thread-safe
    // for concurrency, it's expected to be updated by a dedicated thread
    // but we guarantee reads are always valid
    //
    // the implementaion is based on Redis' zset, but customized for our
    // use
    class SkipList {
    public:

    };
}
#endif
