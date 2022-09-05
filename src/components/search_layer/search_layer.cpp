#include "search_layer.hpp"
namespace DiStore::SearchLayer {
    static auto random_level() -> int {
        static const int threshold = 0.25 * RAND_MAX;
        int level = 1;
        while (random() < threshold)
            level += 1;
        return (level < Constants::MAX_LEVEL) ? level : Constants::MAX_LEVEL;
    }

    auto SkipList::insert(const std::string &anchor, const RemotePointer &r) noexcept -> bool {
        SkipListNode *update[Constants::MAX_LEVEL] = {nullptr};
        auto *walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < anchor) {
                walker = walker->forwards[i];
            }

            update[i] = walker;
        }

        auto level = random_level();
        if (level > current_level) {
            for (auto i = current_level; i < level; i++) {
                update[i] = head;
            }

            current_level = level;
        }

        auto new_node = SkipListNode::make_skip_node(level, anchor, r);

        for (int i = 0; i < level; i++) {
            new_node->forwards[i] = update[i]->forwards[i];
            update[i]->forwards[i] = new_node;
        }

        return true;
    }

    auto SkipList::update(const std::string &anchor, const RemotePointer &r) noexcept -> bool {
        auto node = search_node(anchor);
        if (!node)
            return false;

        node->data_node = r;
        return true;
    }

    auto SkipList::search(const std::string &anchor) const noexcept -> RemotePointer {
        auto node = search_node(anchor);
        if (node)
            return node->data_node;
        return nullptr;
    }

    auto SkipList::dump() const noexcept -> void {
        for (int i = current_level - 1; i >= 0; i--) {
            auto walker = head;
            while (walker->forwards[i]) {
                walker = walker->forwards[i];
                std::cout << walker->anchor << " ";
            }
            std::cout << "\n";
        }

    }

    auto SkipList::search_node(const std::string &anchor) const noexcept -> SkipListNode * {
        SkipListNode *walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < anchor) {
                walker = walker->forwards[i];
            }
        }

        if (walker->forwards[0]->anchor == anchor) {
            return walker->forwards[0];
        }

        return nullptr;
    }
}
