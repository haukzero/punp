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

        struct ProtectedInterval {
            size_t start_first;      // Position of the first char of start marker
            size_t end_last;         // Position of the last char of end marker
            size_t start_marker_len; // Length of start marker
            size_t end_marker_len;   // Length of end marker

            ProtectedInterval(size_t s_first, size_t e_last, size_t s_len, size_t e_len)
                : start_first(s_first), end_last(e_last), start_marker_len(s_len), end_marker_len(e_len) {}

            // Get the position to jump to (right after end marker)
            size_t skip_to() const {
                return end_last + 1;
            }
        };

        Node *root = nullptr;
        ProtectedRegions _protected_regions;

        // Build protected intervals from text (pre-processing)
        std::vector<ProtectedInterval> build_protected_intervals(const std::wstring &text) const;

        // Find the protected interval starting at the given position (if any)
        // Returns pointer to the interval if pos is at the first char of a start marker, nullptr otherwise
        const ProtectedInterval *find_protected_interval_at(const std::vector<ProtectedInterval> &intervals,
                                                            size_t pos) const;

        void clear();

    public:
        ACAutomaton();
        ~ACAutomaton();

        void build_from_map(const ReplacementMap &rep_map);
        void set_protected_regions(const ProtectedRegions &regions);
        size_t apply_replace(std::wstring &text) const;
    };
} // namespace punp
