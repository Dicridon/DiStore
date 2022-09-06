#include "search_layer.hpp"
namespace DiStore::SearchLayer {
    static auto random_level() -> int {
        static const int threshold = 0.25 * RAND_MAX;
        int level = 1;
        while (random() < threshold)
            level += 1;
        return (level < Constants::MAX_LEVEL) ? level : Constants::MAX_LEVEL;
    }

    auto SkipList::insert(const std::string &anchor, const RemotePointer &r, size_t size) noexcept -> bool {
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

        auto new_node = SkipListNode::make_skip_node(level, anchor, r, size);

        for (int i = 0; i < level; i++) {
            new_node->forwards[i] = update[i]->forwards[i];
            update[i]->forwards[i] = new_node;
        }

        return true;
    }

    auto SkipList::update(const std::string &anchor, const RemotePointer &r, size_t s) noexcept -> bool {
        auto node = search_node(anchor);
        if (!node)
            return false;

        node->data_node = r;
        node->data_size = s;
        return true;
    }

    auto SkipList::search(const std::string &anchor) const noexcept -> std::pair<RemotePointer, size_t> {
        auto node = search_node(anchor);
        if (node)
            return {node->data_node, node->data_size};
        return {nullptr, 0};
    }

    auto SkipList::fuzzy_search(const std::string &member) -> std::pair<RemotePointer, size_t> {
        auto walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < member) {
                walker = walker->forwards[i];
            }
        }

        if (walker->forwards[0]->anchor == member) {
            return {walker->forwards[0]->data_node, walker->forwards[0]->data_size};
        }

        if (walker->forwards[0]->anchor > member && walker->anchor <= member)
            return {walker->data_node, walker->data_size};
        return {nullptr, 0};
    }

    auto SkipList::remove(const std::string &anchor) -> bool {
        SkipListNode *update[Constants::MAX_LEVEL] = {nullptr};
        auto *walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < anchor) {
                walker = walker->forwards[i];
            }

            update[i] = walker;
        }

        if (walker->forwards[0]->anchor != anchor)
            return false;

        walker = walker->forwards[0];
        for (int i = 0; i < current_level; i++) {
            if (update[i]->forwards[i] != walker)
                break;

            update[i]->forwards[i] = walker->forwards[i];
        }

        while (current_level > 0 && head->forwards[current_level - 1] == nullptr) {
            --current_level;
        }

        delete walker;

        return true;
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
        auto walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < anchor) {
                walker = walker->forwards[i];
            }
        }

        if (walker->forwards[0] == nullptr)
            return nullptr;

        if (walker->forwards[0]->anchor == anchor) {
            return walker->forwards[0];
        }

        return nullptr;
    }
}
