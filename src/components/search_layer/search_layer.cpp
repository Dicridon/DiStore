#include "search_layer.hpp"
namespace DiStore::SearchLayer {
    auto SkipList::insert(const std::string &anchor, const RemotePointer &r, DataLayer::LinkedNodeType t) noexcept -> bool {
        SkipListNode *update[Constants::MAX_LEVEL] = {nullptr};
        auto *walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < anchor) {
                walker = walker->forwards[i];
            }

            update[i] = walker;
        }

        auto [new_node, level] = SkipList::make_new_node(anchor, r, t);
        if (level > current_level) {
            for (auto i = current_level; i < level; i++) {
                update[i] = head;
            }

            current_level = level;
        }


        for (int i = 0; i < level; i++) {
            new_node->forwards[i] = update[i]->forwards[i];
            update[i]->forwards[i] = new_node;
        }

        new_node->backward = (update[0] == head) ? nullptr : update[0];
        if (new_node->forwards[0])
            new_node->forwards[0]->backward = new_node;
        return true;
    }

    auto SkipList::update(const std::string &anchor, const RemotePointer &r, DataLayer::LinkedNodeType t) noexcept -> bool {
        auto node = search_node(anchor);
        if (!node)
            return false;

        node->data_node = r;
        node->type = t;
        return true;
    }

    auto SkipList::search(const std::string &anchor) const noexcept -> SkipListNode * {
        auto node = search_node(anchor);
        if (node)
            return node;
        return nullptr;
    }

    auto SkipList::calibrate(SkipListNode *node, int level) -> void {
        SkipListNode *update[Constants::MAX_LEVEL] = {nullptr};
        auto *walker = head;

        for (int i = current_level - 1; i >= 1; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < node->anchor) {
                walker = walker->forwards[i];
            }

            update[i] = walker;
        }

        for (int i = 1; i < level; i++) {
            node->forwards[i] = update[i]->forwards[i];
            update[i]->forwards[i] = node;
        }
    }

    auto SkipList::fuzzy_search(const std::string &member) -> SkipListNode * {
        auto walker = head;

        for (int i = current_level - 1; i >= 0; i--) {
            while (walker->forwards[i] && walker->forwards[i]->anchor < member) {
                walker = walker->forwards[i];
            }
        }

        if (walker->forwareds[0] && walker->forwards[0]->anchor == member) {
            return walker->forwards[0];
        }

        if (walker->forwards[0] == nullptr ||
            (walker->forwards[0]->anchor > member && walker->anchor <= member))
            return walker;

        return nullptr;
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

        if (walker->forwards[0])
            walker->forwards[0]->backward = walker->backward;

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
                // std::cout << walker->backward << " <- " << walker << ": ";
                // std::cout << walker->anchor << "\n";
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
