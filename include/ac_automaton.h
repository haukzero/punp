#pragma once

#include "types.h"

namespace punp {
    class ACAutomaton {
    private:
        struct Node {
            std::unordered_map<wchar_t, Node *> children;
            Node *fail = nullptr;

            std::wstring replacement;
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

    public:
        ACAutomaton();
        ~ACAutomaton();

        void build_from_map(const ReplacementMap &rep_map);
        size_t apply_replace(std::wstring &text) const;
    };
} // namespace punp
