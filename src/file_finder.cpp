#include "file_finder.h"
#include "color_print.h"
#include "common.h"
#include <algorithm>
#include <filesystem>

namespace punp {

    std::vector<std::string> FileFinder::find_files(
        const std::vector<std::string> &patterns,
        bool recursive,
        const std::vector<std::string> &extensions,
        const std::vector<std::string> &exclude_paths) const {

        std::vector<std::string> all_files;

        auto expand_one_pattern =
            [this, recursive, &exclude_paths](const std::string &pattern) {
                std::vector<std::string> matched_files;
                if (is_dir(pattern)) {
                    matched_files = find_files_in_dir(pattern, recursive, exclude_paths);
                } else if (contains_wildcard(pattern)) {
                    matched_files = expand_glob(pattern);
                } else if (is_file(pattern)) {
                    matched_files.emplace_back(pattern);
                } else {
                    warn("'", pattern, "' not found");
                }
                return matched_files;
            };

        for (const auto &pattern : patterns) {
            std::vector<std::string> matched_files;
            matched_files = expand_one_pattern(pattern);

            // Ignore the rule file
            matched_files.erase(
                std::remove_if(matched_files.begin(), matched_files.end(),
                               [](const std::string &file) {
                                   return file.find(RuleFile::NAME) != std::string::npos;
                               }),
                matched_files.end());

            // Append to result
            all_files.insert(all_files.end(), matched_files.begin(), matched_files.end());
        }

        // Remove duplicates and sort
        std::sort(all_files.begin(), all_files.end());
        all_files.erase(std::unique(all_files.begin(), all_files.end()), all_files.end());

        // Filter by extension if specified
        if (!extensions.empty()) {
            all_files = filter_by_extension(all_files, extensions);
        }

        // Filter out excluded files/dirs
        if (!exclude_paths.empty()) {
            all_files.erase(
                std::remove_if(all_files.begin(), all_files.end(),
                               [this, &exclude_paths](const std::string &file) {
                                   return is_excluded(file, exclude_paths);
                               }),
                all_files.end());
        }

        return all_files;
    }

    std::vector<std::string> FileFinder::find_files_in_dir(
        const std::string &dir,
        bool recursive,
        const std::vector<std::string> &exclude_paths) const {

        std::vector<std::string> files;

        auto handle_entry =
            [this, &files, &exclude_paths](const std::filesystem::directory_entry &entry) {
                try {
                    const auto p = entry.path();
                    auto s = p.string();
                    if (is_excluded(s, exclude_paths)) {
                        return; // caller will handle recursion disabling for directories
                    }
                    if (entry.is_regular_file()) {
                        files.emplace_back(s);
                    }
                } catch (const std::filesystem::filesystem_error &e) {
                    error("Accessing entry '", entry.path().string(), "': ", e.what());
                }
            };

        try {
            if (recursive) {
                std::filesystem::recursive_directory_iterator it(dir);
                std::filesystem::recursive_directory_iterator end;
                for (; it != end; ++it) {
                    try {
                        const auto &entry = *it;
                        if (is_excluded(entry.path().string(), exclude_paths)) {
                            if (entry.is_directory()) {
                                it.disable_recursion_pending();
                            }
                            continue;
                        }
                        handle_entry(entry);
                    } catch (const std::filesystem::filesystem_error &e) {
                        error("Accessing entry '", it->path().string(), "': ", e.what());
                        continue;
                    }
                }
            } else {
                files = get_files_from_dir(dir);
                // Filter non-recursive files by excludes
                if (!exclude_paths.empty()) {
                    files.erase(
                        std::remove_if(files.begin(), files.end(),
                                       [this, &exclude_paths](const std::string &file) {
                                           return is_excluded(file, exclude_paths);
                                       }),
                        files.end());
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            error("Accessing directory '", dir, "': ", e.what());
        }

        return files;
    }

    bool FileFinder::contains_wildcard(const std::string &s) const {
        return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
    }

    std::string FileFinder::strip_trailing_slashes(std::string s) const {
        while (!s.empty() && (s.back() == '/' || s.back() == '\\')) {
            s.pop_back();
        }
        return s;
    }

    std::vector<std::string> FileFinder::expand_glob(const std::string &pattern) const {
        std::vector<std::string> matches;

        // Extract directory and filename pattern
        size_t last_slash = pattern.find_last_of("/\\");
        std::string dir = ".";
        std::string file_pattern = pattern;

        if (last_slash != std::string::npos) {
            dir = pattern.substr(0, last_slash);
            file_pattern = pattern.substr(last_slash + 1);
        }

        try {
            for (const auto &entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (match_glob(filename, file_pattern)) {
                        matches.emplace_back(entry.path().string());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            error("Expanding glob '", pattern, "': ", e.what());
        }

        return matches;
    }

    bool FileFinder::is_dir(const std::string &path) const {
        try {
            return std::filesystem::is_directory(path);
        } catch (const std::filesystem::filesystem_error &) {
            return false;
        }
    }

    bool FileFinder::is_file(const std::string &path) const {
        try {
            return std::filesystem::is_regular_file(path);
        } catch (const std::filesystem::filesystem_error &) {
            return false;
        }
    }

    bool FileFinder::match_glob(const std::string &filename, const std::string &pattern) const {
        // Simple glob matching for * and ?
        size_t pattern_pos = 0;
        size_t filename_pos = 0;

        while (pattern_pos < pattern.length() && filename_pos < filename.length()) {
            char pat_char = pattern[pattern_pos];

            if (pat_char == '*') {
                // Handle wildcard
                if (pattern_pos + 1 >= pattern.length()) {
                    // * at end matches everything
                    return true;
                }

                // Find next non-wildcard character in pattern
                char next_char = pattern[pattern_pos + 1];

                // Find this character in filename
                while (filename_pos < filename.length() && filename[filename_pos] != next_char) {
                    filename_pos++;
                }

                if (filename_pos >= filename.length()) {
                    return false;
                }

                pattern_pos++;
            } else if (pat_char == '?') {
                // ? matches any single character
                pattern_pos++;
                filename_pos++;
            } else {
                // Regular character match
                if (filename[filename_pos] != pat_char) {
                    return false;
                }
                pattern_pos++;
                filename_pos++;
            }
        }

        // Handle remaining pattern
        while (pattern_pos < pattern.length() && pattern[pattern_pos] == '*') {
            pattern_pos++;
        }

        return pattern_pos >= pattern.length() && filename_pos >= filename.length();
    }

    std::vector<std::string> FileFinder::get_files_from_dir(const std::string &directory) const {
        std::vector<std::string> files;

        try {
            for (const auto &entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            error("Reading directory '", directory, "': ", e.what());
        }

        return files;
    }

    bool FileFinder::has_extension(const std::string &path, const std::vector<std::string> &extensions) const {
        try {
            std::filesystem::path file_path(path);
            std::string ext = file_path.extension().string();

            // Remove leading dot from extension
            if (!ext.empty() && ext.front() == '.') {
                ext = ext.substr(1);
            }

            // Check if this extension is in the list
            for (const auto &target_ext : extensions) {
                if (ext == target_ext) {
                    return true;
                }
            }
            return false;
        } catch (const std::exception &) {
            return false;
        }
    }

    std::vector<std::string> FileFinder::filter_by_extension(
        const std::vector<std::string> &files,
        const std::vector<std::string> &extensions) const {

        std::vector<std::string> filtered;
        filtered.reserve(files.size());

        for (const auto &file : files) {
            if (has_extension(file, extensions)) {
                filtered.emplace_back(file);
            }
        }

        return filtered;
    }

    bool FileFinder::is_excluded(const std::string &file, const std::vector<std::string> &excludes) const {
        namespace fs = std::filesystem;

        auto match_suffixes =
            [this](const fs::path &file_abs, const std::string &pattern) {
                std::vector<fs::path> components;
                for (const auto &c : file_abs)
                    components.push_back(c);
                for (size_t i = 0; i < components.size(); ++i) {
                    fs::path suffix_path;
                    for (size_t j = i; j < components.size(); ++j)
                        suffix_path /= components[j];
                    if (match_glob(suffix_path.string(), pattern))
                        return true;
                }
                return false;
            };

        try {
            fs::path file_path(file);
            auto file_abs = fs::absolute(file_path).lexically_normal();
            std::string filename = file_path.filename().string();

            for (const auto &ex_in : excludes) {
                std::string ex = strip_trailing_slashes(ex_in);
                if (ex.empty())
                    continue;

                // If it contains wildcard
                if (contains_wildcard(ex)) {
                    if (ex.find('/') != std::string::npos || ex.find('\\') != std::string::npos) {
                        fs::path ex_path(ex);
                        if (ex_path.is_absolute()) {
                            auto ex_abs_path = fs::absolute(ex_path).lexically_normal();
                            if (match_glob(file_abs.string(), ex_abs_path.string()))
                                return true;
                        } else {
                            if (match_suffixes(file_abs, ex))
                                return true;
                        }
                    } else {
                        // wildcard without path: match file name and any path component name
                        if (match_glob(filename, ex))
                            return true;
                        for (const auto &comp : file_path) {
                            if (match_glob(comp.string(), ex))
                                return true;
                        }
                    }
                } else {
                    // No wildcard: check exact path, directory prefix, or component/name match
                    fs::path ex_path(ex);
                    auto ex_abs = fs::absolute(ex_path).lexically_normal();
                    if (ex_abs == file_abs)
                        return true;

                    if (fs::exists(ex_abs) && fs::is_directory(ex_abs)) {
                        std::string dir_str = ex_abs.string();
                        if (!dir_str.empty() && dir_str.back() != fs::path::preferred_separator)
                            dir_str.push_back(fs::path::preferred_separator);
                        auto file_str = file_abs.string();
                        if (file_str.rfind(dir_str, 0) == 0)
                            return true;
                    }

                    if (!ex_path.is_absolute()) {
                        for (const auto &comp : file_path) {
                            if (comp.string() == ex)
                                return true;
                        }
                    }

                    if (ex == filename)
                        return true;
                }
            }
        } catch (const std::exception &e) {
            error("Checking exclude for '", file, "' failed: ", e.what());
            return false;
        }

        return false;
    }

} // namespace punp
