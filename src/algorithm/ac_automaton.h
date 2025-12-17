#pragma once

#include "base/types.h"

namespace punp {
    class ACAutomaton {
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
        ProtectedRegions _protected_regions;

        void clear();

    public:
        ACAutomaton();
        ~ACAutomaton();

        void build_from_map(const ReplacementMap &rep_map);
        void set_protected_regions(const ProtectedRegions &regions);

        size_t apply_replace(text_t &text, const size_t page_offset,
                             const GlobalProtectedIntervals &global_intervals) const;

        // Build global protected intervals for entire file content
        GlobalProtectedIntervals build_global_protected_intervals(const text_t &text) const;
    };
} // namespace punp
