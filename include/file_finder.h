#pragma once

#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace punp {
    class FileFinder {
    public:
        FileFinder() = default;
        ~FileFinder() = default;

        std::vector<std::string> find_files(
            const std::vector<std::string> &patterns,
            bool recursive = false,
            const std::vector<std::string> &extensions = {},
            const std::vector<std::string> &exclude_paths = {}) const;

    private:
        struct ExcludeRules {
            std::unordered_set<std::string> names;
            std::vector<std::string> name_globs;
            std::unordered_set<std::string> abs_paths;
            std::vector<std::string> abs_path_globs;
            std::vector<std::string> suffix_globs;
        };

        bool is_dir(const std::string &path) const;
        bool is_file(const std::string &path) const;
        bool contains_wildcard(const std::string &s) const;
        bool match_glob(const std::string &filename, const std::string &pattern) const;
        bool has_extension(const std::string &path, const std::unordered_set<std::string> &extensions) const;

        std::string strip_trailing_slashes(std::string s) const;
        std::vector<std::string> expand_glob(const std::string &pattern) const;
        ExcludeRules parse_excludes(const std::vector<std::string> &excludes) const;
        bool is_excluded(
            const std::filesystem::path &path,
            const ExcludeRules &rules,
            bool check_components = false) const;
        std::vector<std::string> filter_by_extension(
            const std::vector<std::string> &files,
            const std::unordered_set<std::string> &extensions) const;
        std::vector<std::string> find_files_in_dir(
            const std::string &dir,
            bool recursive,
            const std::unordered_set<std::string> &extensions,
            const ExcludeRules &rules) const;
    };

} // namespace punp
