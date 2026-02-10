#pragma once

#include "base/types.h"
#include <vector>

namespace punp {

    struct Match {
        size_t pattern_id; // Index in the patterns vector used for build
        size_t start;      // Start index in text
        size_t end;        // End index in text (exclusive)

        size_t length() const { return end - start; }
        bool is_empty() const { return start == end; }

        // Comparison for sorting: primary start (asc), secondary length (desc)
        bool operator<(const Match &other) const {
            if (start != other.start)
                return start < other.start;
            return length() > other.length();
        }
    };

    class AhoCorasick {
    public:
        AhoCorasick();
        ~AhoCorasick();

        // Build the automaton from a list of patterns.
        void build(const std::vector<text_t> &patterns);

        // Find all non-overlapping matches using leftmost-longest rule.
        // Returns a vector of matches sorted by position.
        std::vector<Match> find_iter(const text_t &text) const;

    private:
        struct Node;
        Node *root = nullptr;

        /////////////////////////////////////////////////////////////////////////////////////////////////////////

        // Helper to collect all overlapping matches
        std::vector<Match> find_overlapping(const text_t &text) const;

        void clear();
    };

} // namespace punp
