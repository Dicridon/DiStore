#ifndef __DISTORE__SEARCH_LAYER__SEARCH_LAYER__
#define __DISTORE__SEARCH_LAYER__SEARCH_LAYER__
#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"

namespace DiStore::SearchLayer {
    using namespace Memory;
    namespace Constants {
        static constexpr int MAX_LEVEL = 16;
    }

    struct SkipListNode {
        std::string anchor;
        RemotePointer data_node;
        SkipListNode *forwards[];

        static auto make_skip_node(int level, const std::string &k, RemotePointer r = nullptr,
                                   SkipListNode *n = nullptr) -> SkipListNode *
        {
            auto buffer = new byte_t[sizeof(SkipListNode) + level * sizeof(SkipListNode *)];
            auto anchor = new (buffer) std::string;
            anchor->assign(k);
            auto ret = reinterpret_cast<SkipListNode *>(buffer);
            ret->forwards[0] = n;
            for (int i = 1; i < level; i++) {
                ret->forwards[i] = nullptr;
            }

            ret->data_node = r;

            return ret;
        }

        SkipListNode() = delete;
        SkipListNode(const SkipListNode &) = delete;
        SkipListNode(SkipListNode &&) = delete;
        auto operator=(const SkipListNode &) = delete;
        auto operator=(SkipListNode &&) = delete;
        ~SkipListNode() = default;
    };


    // This skip list is not thread-safe
    // for concurrency, it's expected to be updated by a dedicated thread
    // but we guarantee reads are always valid
    //
    // the implementaion is based on Redis' zset, but customized for our
    // use
    class SkipList {
    public:
        SkipList() : current_level(1) {
            head = SkipListNode::make_skip_node(Constants::MAX_LEVEL, "");
        };
        SkipList(const SkipList &) = delete;
        SkipList(SkipList &&) = delete;
        auto operator=(const SkipList &) = delete;
        auto operator=(SkipList &&) = delete;
        ~SkipList() = default;

        static auto make_skiplist() -> std::unique_ptr<SkipList> {
            return std::make_unique<SkipList>();
        }

        auto insert(const std::string &anchor, const RemotePointer &r) noexcept -> bool;
        auto update(const std::string &anchor, const RemotePointer &r) noexcept -> bool;
        auto search(const std::string &anchor) const noexcept -> RemotePointer;

        // delete and range are not needed
    private:
        int current_level;
        SkipListNode *head;

        auto search_node(const std::string &anchor) const noexcept -> SkipListNode *;
    };
}
#endif
