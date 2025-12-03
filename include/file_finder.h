#pragma once

#include <string>
#include <vector>

namespace punp {
    class FileFinder {
    private:
        bool is_dir(const std::string &path) const;
        bool is_file(const std::string &path) const;
        bool contains_wildcard(const std::string &s) const;
        std::string strip_trailing_slashes(std::string s) const;
        bool match_glob(const std::string &filename, const std::string &pattern) const;
        std::vector<std::string> get_files_from_dir(const std::string &dir) const;
        bool has_extension(const std::string &path, const std::vector<std::string> &extensions) const;
        bool is_excluded(const std::string &file, const std::vector<std::string> &excludes) const;

    public:
        FileFinder() = default;
        ~FileFinder() = default;

        std::vector<std::string> find_files(
            const std::vector<std::string> &patterns,
            bool recursive = false,
            const std::vector<std::string> &extensions = {},
            const std::vector<std::string> &exclude_paths = {}) const;

        std::vector<std::string> find_files_in_dir(
            const std::string &dir,
            bool recursive = false,
            const std::vector<std::string> &exclude_paths = {}) const;

        std::vector<std::string> expand_glob(const std::string &pattern) const;

        std::vector<std::string> filter_by_extension(
            const std::vector<std::string> &files,
            const std::vector<std::string> &extensions) const;
    };

} // namespace punp
