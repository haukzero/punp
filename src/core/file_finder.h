#pragma once

#include "base/types.h"

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace punp {
    class FileFinder {
    public:
        FileFinder() = default;
        ~FileFinder() = default;

        std::vector<std::string> find_files(const FileFinderConfig &config) const;

    private:
        struct ExcludeRules {
            std::unordered_set<std::string> names;
            std::unordered_set<std::string> extensions;
            std::vector<std::string> name_globs;
            std::unordered_set<std::string> abs_paths;
            std::vector<std::string> abs_path_globs;
            std::vector<std::string> suffix_globs;
            bool ignore_hidden = false;
        };

        std::vector<std::string> expand_pattern(
            const std::string &pattern,
            bool recursive,
            const std::unordered_set<std::string> &ext_set,
            const ExcludeRules &rules) const;

        /**** glob matching ****/
        bool contains_wildcard(const std::string &s) const;
        bool contains_doublestar(const std::string &pattern) const;
        bool match_glob(const std::string &filename, const std::string &pattern) const;

        std::vector<std::string> expand_glob(const std::string &pattern, bool ignore_hidden) const;
        void expand_glob_recursive(
            const std::filesystem::path &current_dir,
            const std::vector<std::string> &pattern_parts,
            size_t part_index,
            bool ignore_hidden,
            std::vector<std::string> &results) const;
        /**** glob matching ****/

        /**** file filtering ****/
        bool has_extension(const std::string &path, const std::unordered_set<std::string> &extensions) const;
        std::vector<std::string> filter_by_extension(
            const std::vector<std::string> &files,
            const std::unordered_set<std::string> &extensions) const;

        ExcludeRules parse_excludes(
            const bool process_hidden = false,
            const std::vector<std::string> &excludes = {}) const;
        bool is_excluded(
            const std::filesystem::path &path,
            const ExcludeRules &rules,
            bool check_components = false) const;
        void generate_default_excludes(
            std::unordered_set<std::string> &names,
            std::unordered_set<std::string> &extensions) const;
        /**** file filtering ****/

        /**** directory traversal ****/
        std::vector<std::string> find_files_in_dir(
            const std::string &dir,
            bool recursive,
            const std::unordered_set<std::string> &extensions,
            const ExcludeRules &rules) const;
        /**** directory traversal ****/

        /**** latex jumping ****/
        void collect_latex_includes(
            const std::string &tex_file,
            const std::filesystem::path &root_dir,
            std::unordered_set<std::string> &visited_files,
            std::unordered_set<std::string> &result_files,
            const ExcludeRules &rules) const;
        std::unordered_set<std::string> extract_latex_includes(const std::string_view &content) const;
        /**** latex jumping ****/

        /**** utils ****/
        bool is_dir(const std::string &path) const;
        bool is_file(const std::string &path) const;

        std::string maybe_expand_tilde(const std::string &path) const;
        std::string strip_trailing_slashes(std::string s) const;
        /**** utils ****/
    };

} // namespace punp
