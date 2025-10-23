#include "ac_automaton.h"
#include "types.h"
#include <algorithm>
#include <queue>

namespace punp {
    ACAutomaton::ACAutomaton() {
        root = new Node();
    }
    ACAutomaton::~ACAutomaton() {
        clear();
    }

    void ACAutomaton::build_from_map(const ReplacementMap &rep_map) {
        clear();
        root = new Node();

        // Build the Trie tree from the replacement map
        for (const auto &pair : rep_map) {
            const std::wstring &pat = pair.first;
            const std::wstring &rep = pair.second;
            if (pat.empty())
                continue;

            Node *cur = root;
            for (wchar_t ch : pat) {
                if (cur->children.find(ch) == cur->children.end()) {
                    cur->children[ch] = new Node();
                }
                cur = cur->children[ch];
            }
            cur->replacement = rep;
            cur->pattern_len = pat.length();
        }

        // NOTE: Simplified failure link construction for non-overlapping patterns
        // Since patterns don't overlap, we only need basic failure links
        std::queue<Node *> q;
        for (auto &pair : root->children) {
            pair.second->fail = root;
            q.emplace(pair.second);
        }

        while (!q.empty()) {
            Node *current = q.front();
            q.pop();

            for (auto &pair : current->children) {
                wchar_t ch = pair.first;
                Node *child = pair.second;

                // NOTE: For non-overlapping patterns, failure typically goes back to root
                // This simplification is valid when patterns don't have common prefixes
                // that could lead to overlapping matches
                child->fail = root;

                // Check if root has a direct transition for this character
                auto root_child = root->children.find(ch);
                if (root_child != root->children.end() && root_child->second != child) {
                    child->fail = root_child->second;
                }

                q.emplace(child);
            }
        }
    }

    void ACAutomaton::set_protected_regions(const ProtectedRegions &regions) {
        _protected_regions = regions;
    }

    std::vector<ACAutomaton::ProtectedInterval> ACAutomaton::build_protected_intervals(const std::wstring &text) const {
        std::vector<ProtectedInterval> intervals;

        if (_protected_regions.empty() || text.empty()) {
            return intervals;
        }

        // Performance optimization: Pre-compute all protected regions
        // Time complexity: O(n * m) where n = text length, m = number of protected regions
        // This is done once per text, not per character

        for (const auto &region : _protected_regions) {
            const std::wstring &start_marker = region.first;
            const std::wstring &end_marker = region.second;

            if (start_marker.empty() || end_marker.empty()) {
                continue;
            }

            // Find all occurrences of start and end markers
            size_t search_pos = 0;
            while (search_pos < text.length()) {
                size_t start_begin = text.find(start_marker, search_pos);
                if (start_begin == std::wstring::npos) {
                    break;
                }

                // Find the corresponding end marker
                size_t end_search_pos = start_begin + start_marker.length();
                size_t end_begin = text.find(end_marker, end_search_pos);

                if (end_begin != std::wstring::npos) {
                    // Calculate position of last char of end marker
                    size_t end_last = end_begin + end_marker.length() - 1;

                    // Store: [start_first, end_last, start_len, end_len]
                    intervals.emplace_back(start_begin,
                                           end_last,
                                           start_marker.length(),
                                           end_marker.length());

                    // Continue searching after this protected region
                    search_pos = end_begin + end_marker.length();
                } else {
                    // No matching end marker, protect till end of text
                    intervals.emplace_back(start_begin,
                                           text.length() - 1,
                                           start_marker.length(),
                                           end_marker.length());
                    break;
                }
            }
        }

        // Sort intervals by start position for efficient lookup
        // Note: We don't merge overlapping intervals here because we need
        // to preserve the exact marker positions for skipping logic
        std::sort(intervals.begin(), intervals.end(),
                  [](const ProtectedInterval &a, const ProtectedInterval &b) {
                      return a.start_first < b.start_first;
                  });

        return intervals;
    }

    const ACAutomaton::ProtectedInterval *ACAutomaton::find_protected_interval_at(
        const std::vector<ProtectedInterval> &intervals,
        size_t pos) const {

        if (intervals.empty()) {
            return nullptr;
        }

        // Use binary search to find interval
        // Check if we're at the first character of a start marker
        auto it = std::lower_bound(intervals.begin(), intervals.end(), pos,
                                   [](const ProtectedInterval &interval, size_t position) {
                                       return interval.start_first < position;
                                   });

        // Check if current position matches a start marker's first char
        if (it != intervals.end() && it->start_first == pos) {
            return &(*it);
        }

        return nullptr;
    }

    size_t ACAutomaton::apply_replace(std::wstring &text) const {
        if (!root || text.empty()) {
            return 0;
        }

        // Pre-compute protected intervals (one-time O(n*m) operation)
        std::vector<ProtectedInterval> protected_intervals = build_protected_intervals(text);

        std::wstring result;
        result.reserve(text.length());

        size_t text_pos = 0;
        size_t replacement_count = 0;

        while (text_pos < text.length()) {
            // Check if we're at the start of a protected region
            // If so, skip the entire protected region in one jump
            const ProtectedInterval *protected_region = find_protected_interval_at(protected_intervals, text_pos);

            if (protected_region != nullptr) {
                // We're at the first char of a start marker
                // Copy the entire protected region including both markers
                size_t copy_start = text_pos;                  // Start from current position
                size_t copy_end = protected_region->skip_to(); // end_last is last char of end marker

                for (size_t i = copy_start; i < copy_end && i < text.length(); ++i) {
                    result += text[i];
                }

                // Jump to position right after the end marker
                text_pos = copy_end;
                continue;
            }

            // Not in a protected region, try to find and apply replacements
            bool found_match = false;
            Node *cur = root;
            size_t match_length = 0;
            std::wstring replacement;

            // Try to find the longest match starting at current position
            for (size_t i = text_pos; i < text.length(); ++i) {
                wchar_t ch = text[i];

                // Check if current character exists in children
                auto it = cur->children.find(ch);
                if (it == cur->children.end()) {
                    // No match possible from this path
                    break;
                }

                cur = it->second;

                // Check if this node represents a complete pattern
                if (cur->pattern_len > 0) {
                    // Found a match - since patterns don't overlap,
                    // this is the only possible match at this position
                    match_length = cur->pattern_len;
                    replacement = cur->replacement;
                    found_match = true;
                    break; // No need to continue as patterns don't overlap
                }
            }

            if (found_match) {
                // Add the replacement
                result += replacement;
                text_pos += match_length;
                replacement_count++;
            } else {
                // No match found, copy original character
                result += text[text_pos];
                text_pos++;
            }
        }

        if (replacement_count > 0) {
            text.swap(result);
        }

        return replacement_count;
    }

    void ACAutomaton::clear() {
        if (root) {
            delete root;
            root = nullptr;
        }
    }
} // namespace punp
