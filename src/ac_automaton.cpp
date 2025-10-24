#include "ac_automaton.h"
#include "types.h"
#include <algorithm>
#include <cstddef>
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
            const text_t &pat = pair.first;
            const text_t &rep = pair.second;
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

    GlobalProtectedIntervals ACAutomaton::build_global_protected_intervals(const text_t &text) const {
        GlobalProtectedIntervals intervals;

        if (_protected_regions.empty() || text.empty()) {
            return intervals;
        }

        // Single-pass scan through text
        size_t pos = 0, text_len = text.length();
        while (pos < text_len) {
            // Early exit if remaining text is shorter than shortest start marker
            if (text_len - pos < _protected_regions.front().first.length()) {
                break;
            }

            // Try to match any start marker at current position
            bool found_start = false;
            const text_t *matched_start = nullptr;
            const text_t *matched_end = nullptr;
            size_t start_pos = pos;

            for (const auto &region_ptrs : _protected_regions) {
                const text_t &start_marker = region_ptrs.first;

                if (pos + start_marker.length() <= text_len) {
                    if (text.compare(pos, start_marker.length(), start_marker) == 0) {
                        matched_start = &start_marker;
                        matched_end = &region_ptrs.second;
                        found_start = true;
                        break;
                    }
                }
            }

            if (found_start) {
                // Found a start marker, now search for corresponding end marker
                size_t end_search_pos = start_pos + matched_start->length();
                size_t end_begin = text.find(*matched_end, end_search_pos);

                if (end_begin == text_t::npos) {
                    break;
                }

                size_t end_last = end_begin + matched_end->length() - 1;
                intervals.emplace_back(start_pos, end_last,
                                       matched_start->length(), matched_end->length());
                pos = end_begin + matched_end->length();
            } else {
                // No start marker at current position, move forward
                ++pos;
            }
        }

        // Sort intervals by start position for efficient lookup
        // Note: We don't merge overlapping intervals here because we need
        // to preserve the exact marker positions for skipping logic
        std::sort(intervals.begin(), intervals.end(),
                  [](const GlobalProtectedInterval &a, const GlobalProtectedInterval &b) {
                      return a.start_first < b.start_first;
                  });

        return intervals;
    }

    size_t ACAutomaton::apply_replace(text_t &text, const size_t page_offset,
                                      const GlobalProtectedIntervals &global_intervals) const {
        if (!root || text.empty()) {
            return 0;
        }

        text_t result;
        result.reserve(text.length());

        size_t text_pos = 0;
        size_t replacement_count = 0;
        size_t page_end = page_offset + text.length();

        size_t copy_start = 0; // Start of the pending copy region
        size_t copy_end = 0;   // End of the pending copy region (exclusive)

        auto flush_copy = [&]() {
            if (copy_end > copy_start) {
                result.reserve(result.length() + (copy_end - copy_start));
                result.append(text.begin() + copy_start, text.begin() + copy_end);
                copy_start = copy_end;
            }
        };

        while (text_pos < text.length()) {
            size_t global_pos = page_offset + text_pos;

            // Check if current position is within any global protected interval
            bool in_protected_region = false;

            for (const auto &interval : global_intervals) {
                // Check if this interval affects current page position
                if (interval.start_first <= global_pos && global_pos <= interval.end_last) {
                    // Current position is inside a protected region
                    in_protected_region = true;

                    // Flush any pending unprotected content before protected region
                    flush_copy();

                    // Calculate how many characters to skip in the page text
                    size_t skip_to_global = interval.skip_to();

                    // Copy protected content until end of interval or end of page
                    size_t copy_until_global = std::min(skip_to_global, page_end);
                    size_t copy_until_local = copy_until_global - page_offset;

                    result.reserve(result.length() + (copy_until_local - text_pos));
                    result.append(text.begin() + text_pos, text.begin() + copy_until_local);

                    text_pos = copy_until_local;
                    copy_start = text_pos;
                    copy_end = text_pos;
                    break;
                }

                // Early exit: intervals are sorted, no need to check further
                if (interval.start_first > global_pos) {
                    break;
                }
            }

            if (in_protected_region) {
                continue;
            }

            // Not in a protected region, try to find and apply replacements
            bool found_match = false;
            Node *cur = root;
            size_t match_length = 0;
            text_t replacement;

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
                // Flush pending copy buffer before adding replacement
                flush_copy();

                // Add the replacement
                result += replacement;
                text_pos += match_length;

                // Update copy pointers to skip matched text
                copy_start = text_pos;
                copy_end = text_pos;

                replacement_count++;
            } else {
                // No match found, extend the copy region
                copy_end = text_pos + 1;
                text_pos++;
            }
        }

        // Flush any remaining pending copy buffer
        flush_copy();

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
