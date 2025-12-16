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
        ExcludeRules rules = parse_excludes(exclude_paths);
        std::unordered_set<std::string> ext_set(extensions.begin(), extensions.end());

        auto expand_one_pattern =
            [this, recursive, &ext_set, &rules](const std::string &pattern) {
                std::vector<std::string> matched_files;
                if (is_dir(pattern)) {
                    matched_files = find_files_in_dir(pattern, recursive, ext_set, rules);
                } else if (contains_wildcard(pattern)) {
                    matched_files = expand_glob(pattern);
                    if (!ext_set.empty()) {
                        matched_files = filter_by_extension(matched_files, ext_set);
                    }
                    // Filter by excludes
                    matched_files.erase(
                        std::remove_if(matched_files.begin(), matched_files.end(),
                                       [this, &rules](const std::string &file) {
                                           return is_excluded(std::filesystem::path(file), rules, true);
                                       }),
                        matched_files.end());
                } else if (is_file(pattern)) {
                    if (ext_set.empty() || has_extension(pattern, ext_set)) {
                        if (!is_excluded(std::filesystem::path(pattern), rules, true)) {
                            matched_files.emplace_back(pattern);
                        }
                    }
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

        return all_files;
    }

    std::vector<std::string> FileFinder::find_files_in_dir(
        const std::string &dir,
        bool recursive,
        const std::unordered_set<std::string> &extensions,
        const ExcludeRules &rules) const {

        std::vector<std::string> files;

        try {
            if (recursive) {
                std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied);
                std::filesystem::recursive_directory_iterator end;
                for (; it != end; ++it) {
                    try {
                        const auto &entry = *it;

                        // Check exclude
                        if (is_excluded(entry.path(), rules, false)) {
                            if (entry.is_directory()) {
                                it.disable_recursion_pending();
                            }
                            continue;
                        }

                        if (entry.is_directory()) {
                            continue;
                        }

                        if (entry.is_regular_file()) {
                            std::string path_str = entry.path().string();
                            if (extensions.empty() || has_extension(path_str, extensions)) {
                                if (path_str.find(RuleFile::NAME) == std::string::npos) {
                                    files.emplace_back(std::move(path_str));
                                }
                            }
                        }
                    } catch (const std::filesystem::filesystem_error &e) {
                        error("Accessing entry '", it->path().string(), "': ", e.what());
                        continue;
                    }
                }
            } else {
                for (const auto &entry : std::filesystem::directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        if (is_excluded(entry.path(), rules, false)) {
                            continue;
                        }

                        std::string path_str = entry.path().string();
                        if (extensions.empty() || has_extension(path_str, extensions)) {
                            if (path_str.find(RuleFile::NAME) == std::string::npos) {
                                files.emplace_back(std::move(path_str));
                            }
                        }
                    }
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

    bool FileFinder::has_extension(const std::string &path, const std::unordered_set<std::string> &extensions) const {
        try {
            std::filesystem::path file_path(path);
            std::string ext = file_path.extension().string();

            // Remove leading dot from extension
            if (!ext.empty() && ext.front() == '.') {
                ext = ext.substr(1);
            }

            // Check if this extension is in the list
            return extensions.count(ext) > 0;
        } catch (const std::exception &) {
            return false;
        }
    }

    std::vector<std::string> FileFinder::filter_by_extension(
        const std::vector<std::string> &files,
        const std::unordered_set<std::string> &extensions) const {

        std::vector<std::string> filtered;
        filtered.reserve(files.size());

        for (const auto &file : files) {
            if (has_extension(file, extensions)) {
                filtered.emplace_back(file);
            }
        }

        return filtered;
    }

    FileFinder::ExcludeRules FileFinder::parse_excludes(const std::vector<std::string> &excludes) const {
        ExcludeRules rules;
        namespace fs = std::filesystem;

        for (const auto &ex_in : excludes) {
            std::string ex = strip_trailing_slashes(ex_in);
            if (ex.empty())
                continue;

            if (contains_wildcard(ex)) {
                if (ex.find('/') != std::string::npos || ex.find('\\') != std::string::npos) {
                    fs::path ex_path(ex);
                    if (ex_path.is_absolute()) {
                        try {
                            auto ex_abs_path = fs::absolute(ex_path).lexically_normal();
                            rules.abs_path_globs.push_back(ex_abs_path.string());
                        } catch (...) {
                            rules.abs_path_globs.push_back(ex);
                        }
                    } else {
                        rules.suffix_globs.push_back(ex);
                    }
                } else {
                    rules.name_globs.push_back(ex);
                }
            } else {
                // No wildcard
                if (ex.find('/') != std::string::npos || ex.find('\\') != std::string::npos) {
                    // Path
                    try {
                        fs::path ex_path(ex);
                        auto ex_abs = fs::absolute(ex_path).lexically_normal();
                        rules.abs_paths.insert(ex_abs.string());
                    } catch (...) {
                        // Ignore invalid paths
                    }
                } else {
                    // Name
                    rules.names.insert(ex);
                }
            }
        }
        return rules;
    }

    bool FileFinder::is_excluded(const std::filesystem::path &path, const ExcludeRules &rules, bool check_components) const {
        namespace fs = std::filesystem;
        std::string filename = path.filename().string();

        // 1. Fast check: Exact Name Match
        if (rules.names.count(filename)) {
            return true;
        }

        // 2. Fast check: Name Glob
        for (const auto &pattern : rules.name_globs) {
            if (match_glob(filename, pattern))
                return true;
        }

        // If we need to check components (e.g. for non-recursive file list), do it here
        if (check_components) {
            for (const auto &comp : path) {
                std::string comp_str = comp.string();
                if (rules.names.count(comp_str)) {
                    return true;
                }
                for (const auto &pattern : rules.name_globs) {
                    if (match_glob(comp_str, pattern))
                        return true;
                }
            }
        }

        // 3. Path checks (Expensive)
        if (rules.abs_paths.empty() && rules.abs_path_globs.empty() && rules.suffix_globs.empty())
            return false;

        // Lazy absolute path
        fs::path abs_path_cache;
        bool abs_path_computed = false;
        auto get_abs_path = [&]() -> const fs::path & {
            if (!abs_path_computed) {
                try {
                    abs_path_cache = fs::absolute(path).lexically_normal();
                } catch (...) {
                    abs_path_cache = path;
                }
                abs_path_computed = true;
            }
            return abs_path_cache;
        };

        if (!rules.abs_paths.empty()) {
            const auto &abs = get_abs_path();
            fs::path current = abs;
            while (!current.empty()) {
                if (rules.abs_paths.count(current.string())) {
                    return true;
                }
                if (!current.has_parent_path() || current == current.parent_path()) {
                    break;
                }
                current = current.parent_path();
            }
        }

        if (!rules.abs_path_globs.empty()) {
            const auto &abs = get_abs_path();
            std::string abs_str = abs.string();
            for (const auto &pattern : rules.abs_path_globs) {
                if (match_glob(abs_str, pattern))
                    return true;
            }
        }

        if (!rules.suffix_globs.empty()) {
            const auto &abs = get_abs_path();
            // Re-implement match_suffixes logic efficiently?
            // Or just use the lambda logic from before
            auto match_suffixes_internal =
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

            for (const auto &pattern : rules.suffix_globs) {
                if (match_suffixes_internal(abs, pattern))
                    return true;
            }
        }

        return false;
    }

} // namespace punp
