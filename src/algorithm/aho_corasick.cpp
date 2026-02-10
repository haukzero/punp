#include "algorithm/aho_corasick.h"

#include <algorithm>
#include <queue>
#include <unordered_map>

namespace punp {

    struct AhoCorasick::Node {
        std::unordered_map<wchar_t, Node *> children;
        Node *fail = nullptr;

        struct PatternInfo {
            size_t id;
            size_t len;
        };
        std::vector<PatternInfo> patterns;

        ~Node() {
            for (auto &pair : children) {
                delete pair.second;
            }
        }
    };

    AhoCorasick::AhoCorasick() {
        root = new Node();
    }

    AhoCorasick::~AhoCorasick() {
        clear();
    }

    void AhoCorasick::clear() {
        if (root) {
            delete root;
            root = nullptr;
        }
    }

    void AhoCorasick::build(const std::vector<text_t> &patterns) {
        clear();
        root = new Node();

        for (size_t i = 0; i < patterns.size(); ++i) {
            const auto &pat = patterns[i];
            if (pat.empty())
                continue;

            Node *curr = root;
            for (auto ch : pat) {
                if (curr->children.find(ch) == curr->children.end()) {
                    curr->children[ch] = new Node();
                }
                curr = curr->children[ch];
            }
            curr->patterns.push_back({i, pat.length()});
        }

        // Build failure links
        std::queue<Node *> q;
        for (auto &pair : root->children) {
            pair.second->fail = root;
            q.push(pair.second);
        }

        while (!q.empty()) {
            Node *curr = q.front();
            q.pop();

            for (auto &pair : curr->children) {
                wchar_t ch = pair.first;
                Node *child = pair.second;
                Node *fail = curr->fail;

                while (fail != nullptr && fail->children.find(ch) == fail->children.end()) {
                    if (fail == root)
                        break;
                    fail = fail->fail;
                }

                if (fail && fail->children.find(ch) != fail->children.end()) {
                    child->fail = fail->children[ch];
                    // Also merge patterns from the node we failed to, because they are suffixes
                    if (child->fail) {
                        child->patterns.insert(child->patterns.end(),
                                               child->fail->patterns.begin(),
                                               child->fail->patterns.end());
                    }
                } else {
                    child->fail = root;
                }

                // If we pointed to root, nothing to merge (unless root matches generic? No)

                q.push(child);
            }
        }
    }

    std::vector<Match> AhoCorasick::find_overlapping(const text_t &text) const {
        std::vector<Match> matches;
        if (!root)
            return matches;

        Node *curr = root;

        for (size_t i = 0; i < text.length(); ++i) {
            wchar_t ch = text[i];

            while (curr != root && curr->children.find(ch) == curr->children.end()) {
                curr = curr->fail;
            }

            if (curr->children.find(ch) != curr->children.end()) {
                curr = curr->children[ch];
            }

            // Collect matches ending here
            for (const auto &p : curr->patterns) {
                matches.push_back({p.id, i + 1 - p.len, i + 1});
            }
        }
        return matches;
    }

    std::vector<Match> AhoCorasick::find_iter(const text_t &text) const {
        auto matches = find_overlapping(text);
        if (matches.empty())
            return {};

        // Sort by start position (asc), then by length (desc) for Leftmost-Longest
        std::sort(matches.begin(), matches.end());

        std::vector<Match> result;
        size_t last_end = 0;

        for (const auto &m : matches) {
            if (m.start >= last_end) {
                result.emplace_back(m);
                last_end = m.end;
            }
        }
        return result;
    }

} // namespace punp
