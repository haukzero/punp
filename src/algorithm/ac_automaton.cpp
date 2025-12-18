#include "algorithm/ac_automaton.h"

#include "base/types.h"

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

    size_t ACAutomaton::apply_replace(text_t &text) const {
        if (!root || text.empty()) {
            return 0;
        }

        text_t result;
        result.reserve(text.length());

        size_t text_pos = 0;
        size_t replacement_count = 0;

        size_t copy_start = 0; // Start of the pending copy region
        size_t copy_end = 0;   // End of the pending copy region (exclusive)

        auto flush_copy = [&result, &text, &copy_start, &copy_end]() {
            if (copy_end > copy_start) {
                result.reserve(result.length() + (copy_end - copy_start));
                result.append(text.begin() + copy_start, text.begin() + copy_end);
                copy_start = copy_end;
            }
        };

        while (text_pos < text.length()) {
            // Try to find and apply replacements
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
