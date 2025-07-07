#pragma once

#include <string>
#include <vector>

namespace punp {
    class FileFinder {
    private:
        bool is_dir(const std::string &path) const;
        bool is_file(const std::string &path) const;
        bool match_glob(const std::string &filename, const std::string &pattern) const;
        std::vector<std::string> get_files_from_dir(const std::string &dir) const;

    public:
        FileFinder() = default;
        ~FileFinder() = default;

        std::vector<std::string> find_files(
            const std::vector<std::string> &patterns,
            bool recursive = false) const;

        std::vector<std::string> find_files_in_dir(
            const std::string &dir,
            bool recursive = false) const;

        std::vector<std::string> expand_glob(const std::string &pattern) const;
    };

} // namespace punp
