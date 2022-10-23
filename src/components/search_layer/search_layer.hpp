#ifndef __DISTORE__SEARCH_LAYER__SEARCH_LAYER__
#define __DISTORE__SEARCH_LAYER__SEARCH_LAYER__
#include "memory/memory.hpp"
#include "memory/remote_memory/remote_memory.hpp"
#include "data_layer/data_layer.hpp"

#include "handover_locktable/handover_locktable.hpp"

#include <atomic>
#include <mutex>


namespace DiStore::SearchLayer {
    using namespace Memory;
    namespace Constants {
        static constexpr int MAX_LEVEL = 16;
    }

    struct SkipListNode {
        std::string anchor;
        RemotePointer data_node;
        DataLayer::LinkedNodeType type;
        std::atomic<Concurrency::ConcurrencyContext *> ctx;
        uint64_t version;
        SkipListNode *backward;
        SkipListNode *forwards[];

        static auto make_skip_node(int level, const std::string &k, RemotePointer r = nullptr,
                                   DataLayer::LinkedNodeType t = DataLayer::LinkedNodeType::NotSet,
                                   SkipListNode *n = nullptr, SkipListNode *b = nullptr)
            -> SkipListNode *
        {
            auto buffer = new byte_t[sizeof(SkipListNode) + level * sizeof(SkipListNode *)];
            auto anchor = new (buffer) std::string;
            anchor->assign(k);
            auto ret = reinterpret_cast<SkipListNode *>(buffer);
            ret->version = 0;
            ret->forwards[0] = n;
            ret->backward = b;
            for (int i = 1; i < level; i++) {
                ret->forwards[i] = nullptr;
            }

            ret->data_node = r;
            ret->type = t;
            ret->ctx = nullptr;

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
            head = SkipListNode::make_skip_node(Constants::MAX_LEVEL, "", nullptr,
                                                DataLayer::LinkedNodeType::TypeHead);
        };
        SkipList(const SkipList &) = delete;
        SkipList(SkipList &&) = delete;
        auto operator=(const SkipList &) = delete;
        auto operator=(SkipList &&) = delete;
        ~SkipList() = default;
        static auto random_level() -> int {
            static const int threshold = 0.25 * RAND_MAX;
            int level = 1;
            while (random() < threshold)
                level += 1;
            return (level < Constants::MAX_LEVEL) ? level : Constants::MAX_LEVEL;
        }

        static auto make_skiplist() -> std::unique_ptr<SkipList> {
            return std::make_unique<SkipList>();
        }

        static auto make_new_node(const std::string &anchor, const RemotePointer &r,
                           DataLayer::LinkedNodeType t) noexcept
            -> std::pair<SkipListNode *, int> {

            auto level = random_level();
            auto new_node = SkipListNode::make_skip_node(level, anchor, r, t);
            return {new_node, level};
        }

        auto insert(const std::string &anchor, const RemotePointer &r, DataLayer::LinkedNodeType t) noexcept
            -> bool;
        auto update(const std::string &anchor, const RemotePointer &r, DataLayer::LinkedNodeType t) noexcept
            -> bool;
        auto search(const std::string &anchor) const noexcept -> SkipListNode *;

        // a new node is linked at the bottom level, but upper levels are not updated
        auto calibrate(SkipListNode *node, int level) -> void;

        // search whether a member key's anchor key is in the list
        // 1 -> 10 -> 20 -> ... -> 100, searching for 3 will return 1
        auto fuzzy_search(const std::string &member) -> SkipListNode *;
        auto remove(const std::string &anchor) -> bool;

        auto dump() const noexcept -> void;

        inline auto iter() const noexcept -> SkipListNode * {
            return head;
        }

        auto show_levels() const noexcept -> void;

        // delete and range are not needed
    private:
        int current_level;
        SkipListNode *head;
        auto search_node(const std::string &anchor) const noexcept -> SkipListNode *;
    };
}
#endif
