#pragma once

#include "base/types.h"

namespace punp {
    class ACAutomaton {
    public:
        explicit ACAutomaton();
        ~ACAutomaton();

        void build_from_map(const ReplacementMap &rep_map);
        size_t apply_replace(text_t &text) const;

    private:
        struct Node {
            std::unordered_map<wchar_t, Node *> children;
            Node *fail = nullptr;

            text_t replacement;
            size_t pattern_len = 0;

            Node() = default;
            ~Node() {
                for (auto &pair : children) {
                    delete pair.second;
                }
            }
        };

        Node *root = nullptr;

        void clear();
    };
} // namespace punp
