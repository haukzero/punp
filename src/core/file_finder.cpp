#include "core/file_finder.h"

#include "base/color_print.h"
#include "base/common.h"
#include "config/default_excludes.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_set>

namespace punp {
    namespace fs = std::filesystem;

    std::vector<std::string> FileFinder::find_files(const FileFinderConfig &config) const {

        ExcludeRules rules = parse_excludes(config.process_hidden, config.exclude_paths);
        std::unordered_set<std::string> ext_set(config.extensions.begin(), config.extensions.end());

        // Deduplicate during collection; return value remains sorted.
        std::unordered_set<std::string> unique_files;
        for (const auto &pattern : config.patterns) {
            const auto expanded_pattern = maybe_expand_tilde(pattern);
            for (auto &file :
                 expand_pattern(expanded_pattern, config.recursive, ext_set, rules)) {
                // Normalize path to canonical form for proper deduplication
                auto normalized = fs::absolute(fs::path(file)).lexically_normal().string();
                unique_files.insert(std::move(normalized));
            }
        }

        // If LaTeX jumping is enabled, recursively collect included files
        if (config.enable_latex_jumping) {
            std::unordered_set<std::string> visited_files;
            std::unordered_set<std::string> latex_files;

            // Collect all .tex files from the initial set as starting points
            std::vector<std::string> initial_tex_files;
            for (const auto &file : unique_files) {
                if (file.size() >= 4 && file.substr(file.size() - 4) == ".tex") {
                    initial_tex_files.push_back(file);
                }
            }

            // Recursively collect all included files from each initial .tex file
            for (const auto &file : initial_tex_files) {
                fs::path root_dir = fs::path(file).parent_path();
                collect_latex_includes(file, root_dir, visited_files, latex_files, rules);
            }

            // Add all collected LaTeX files to the unique set
            for (auto &file : latex_files) {
                // Normalize path to canonical form for proper deduplication
                auto normalized = fs::absolute(fs::path(file)).lexically_normal().string();
                unique_files.insert(std::move(normalized));
            }
        }

        std::vector<std::string> all_files;
        all_files.reserve(unique_files.size());
        for (auto &file : unique_files) {
            all_files.emplace_back(std::move(file));
        }

        std::sort(all_files.begin(), all_files.end());

        return all_files;
    }

    std::vector<std::string> FileFinder::expand_pattern(
        const std::string &pattern,
        bool recursive,
        const std::unordered_set<std::string> &ext_set,
        const ExcludeRules &rules) const {

        auto should_keep = [&](const std::string &path_str) {
            if (!ext_set.empty() && !has_extension(path_str, ext_set)) {
                return false;
            }
            return !is_excluded(fs::path(path_str), rules, true);
        };

        if (is_dir(pattern)) {
            return find_files_in_dir(pattern, recursive, ext_set, rules);
        }

        if (contains_wildcard(pattern)) {
            auto matched = expand_glob(pattern, rules.ignore_hidden);
            matched.erase(
                std::remove_if(matched.begin(), matched.end(),
                               [&](const std::string &file) { return !should_keep(file); }),
                matched.end());
            return matched;
        }

        if (is_file(pattern)) {
            if (should_keep(pattern)) {
                return {pattern};
            }
            return {};
        }

        warn("'", pattern, "' not found");
        return {};
    }

    bool contains_path_separator(std::string_view s) {
        return s.find('/') != std::string_view::npos || s.find('\\') != std::string_view::npos;
    }

    bool contains_rule_file_name(std::string_view path_str) {
        return path_str.find(RuleFile::NAME) != std::string_view::npos;
    }

    bool is_hidden_name(std::string_view name) {
        return !name.empty() && name.front() == '.';
    }

    std::vector<std::string> split_glob_pattern_parts(const std::string &pattern) {
        std::vector<std::string> parts;
        std::string current;
        parts.reserve(8);

        for (char c : pattern) {
            if (c == '/' || c == '\\') {
                if (!current.empty()) {
                    parts.emplace_back(std::move(current));
                    current.clear();
                }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) {
            parts.emplace_back(std::move(current));
        }

        return parts;
    }

    bool FileFinder::contains_wildcard(const std::string &s) const {
        return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
    }

    bool FileFinder::contains_doublestar(const std::string &pattern) const {
        return pattern.find("**") != std::string::npos;
    }

    bool FileFinder::match_glob(const std::string &filename, const std::string &pattern) const {
        const size_t fn_len = filename.size();
        const size_t pt_len = pattern.size();

        // Wildcard matching with O(|pattern|) memory:
        // dp[j] means: filename[0..i) matches pattern[0..j)
        std::vector<char> dp(pt_len + 1, 0);
        dp[0] = 1;

        // Empty filename: only a prefix of '*' can match.
        for (size_t j = 1; j <= pt_len; ++j) {
            dp[j] = (pattern[j - 1] == '*') ? dp[j - 1] : 0;
        }

        for (size_t i = 1; i <= fn_len; ++i) {
            char prev_diag = dp[0];
            dp[0] = 0;
            for (size_t j = 1; j <= pt_len; ++j) {
                const char prev_row = dp[j];
                const char p = pattern[j - 1];

                if (p == '*') {
                    // '*' matches empty (dp[i][j-1]) or one more char (dp[i-1][j]).
                    dp[j] = static_cast<char>(dp[j] || dp[j - 1]);
                } else {
                    const char f = filename[i - 1];
                    dp[j] = static_cast<char>(prev_diag && (p == '?' || p == f));
                }

                prev_diag = prev_row;
            }
        }

        return dp[pt_len] != 0;
    }

    std::vector<std::string> FileFinder::expand_glob(const std::string &pattern, bool ignore_hidden) const {
        std::vector<std::string> matches;

        // Check if pattern contains `**`
        if (contains_doublestar(pattern)) {
            auto parts = split_glob_pattern_parts(pattern);

            // Determine starting directory
            fs::path start_dir = ".";
            size_t start_index = 0;

            // If pattern starts with `/`, it's absolute
            if (!pattern.empty() && pattern[0] == '/') {
                start_dir = "/";
            } else if (!parts.empty() && parts[0] == ".") {
                start_index = 1;
            }

            expand_glob_recursive(
                start_dir,
                parts,
                start_index,
                ignore_hidden,
                matches);
        } else {
            size_t last_slash = pattern.find_last_of("/\\");
            std::string dir = ".";
            std::string file_pattern = pattern;

            if (last_slash != std::string::npos) {
                dir = pattern.substr(0, last_slash);
                file_pattern = pattern.substr(last_slash + 1);
            }

            std::error_code ec;
            fs::directory_iterator it(dir, ec);
            if (ec) {
                error("Expanding glob '", pattern, "': ", ec.message());
                return matches;
            }

            for (const auto &entry : it) {
                const auto filename = entry.path().filename().string();
                if (ignore_hidden && is_hidden_name(filename)) {
                    continue;
                }

                std::error_code ec2;
                if (!entry.is_regular_file(ec2) || ec2) {
                    continue;
                }

                if (match_glob(filename, file_pattern)) {
                    matches.emplace_back(entry.path().string());
                }
            }
        }

        return matches;
    }

    void FileFinder::expand_glob_recursive(
        const std::filesystem::path &current_dir,
        const std::vector<std::string> &pattern_parts,
        size_t part_index,
        bool ignore_hidden,
        std::vector<std::string> &results) const {

        auto should_skip = [&](const fs::path &p) {
            if (!ignore_hidden) {
                return false;
            }
            return is_hidden_name(p.filename().string());
        };

        // Base case: we've matched all parts
        if (part_index >= pattern_parts.size()) {
            return;
        }

        const std::string &current_part = pattern_parts[part_index];

        // Handle `**` - recursive directory matching
        if (current_part == "**") {
            // If `**` is the last part, collect all files recursively
            if (part_index == pattern_parts.size() - 1) {
                std::error_code ec;
                for (fs::recursive_directory_iterator it(
                         current_dir,
                         fs::directory_options::skip_permission_denied,
                         ec);
                     !ec && it != fs::recursive_directory_iterator();
                     it.increment(ec)) {

                    const fs::path p = it->path();
                    if (should_skip(p)) {
                        std::error_code ec_dir;
                        if (it->is_directory(ec_dir) && !ec_dir) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    std::error_code ec_file;
                    if (it->is_regular_file(ec_file) && !ec_file) {
                        results.emplace_back(p.string());
                    }
                }
                return;
            }

            // `**` in the middle: try matching at current level and all subdirectories
            // First, try to continue matching from current directory
            expand_glob_recursive(current_dir,
                                  pattern_parts,
                                  part_index + 1,
                                  ignore_hidden,
                                  results);

            // Then recursively try all subdirectories
            std::error_code ec;
            for (fs::directory_iterator it(current_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
                const auto &entry = *it;
                const fs::path p = entry.path();
                if (should_skip(p)) {
                    continue;
                }

                std::error_code ec_dir;
                if (entry.is_directory(ec_dir) && !ec_dir) {
                    expand_glob_recursive(p, pattern_parts, part_index, ignore_hidden, results);
                }
            }
            return;
        }

        // Handle normal pattern (possibly with `*` or `?`)
        bool is_last_part = (part_index == pattern_parts.size() - 1);

        std::error_code ec;
        for (fs::directory_iterator it(current_dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const auto &entry = *it;
            const fs::path p = entry.path();

            const auto entry_name = p.filename().string();
            if (ignore_hidden && is_hidden_name(entry_name)) {
                continue;
            }

            if (!match_glob(entry_name, current_part)) {
                continue;
            }

            if (is_last_part) {
                std::error_code ec_file;
                if (entry.is_regular_file(ec_file) && !ec_file) {
                    results.emplace_back(p.string());
                }
            } else {
                std::error_code ec_dir;
                if (entry.is_directory(ec_dir) && !ec_dir) {
                    expand_glob_recursive(p, pattern_parts, part_index + 1, ignore_hidden, results);
                }
            }
        }
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

    FileFinder::ExcludeRules FileFinder::parse_excludes(
        const bool process_hidden,
        const std::vector<std::string> &excludes) const {

        ExcludeRules rules;
        rules.ignore_hidden = !process_hidden;
        // Ignore the rule files
        rules.names.insert(RuleFile::NAME);

        if (!process_hidden) {
            generate_default_excludes(rules.names, rules.extensions);
        }

        for (const auto &ex_in : excludes) {
            std::string ex = strip_trailing_slashes(ex_in);
            if (ex.empty())
                continue;

            const bool has_wildcards = contains_wildcard(ex);
            const bool is_path_like = contains_path_separator(ex);

            // Name-only rules.
            if (!is_path_like) {
                if (has_wildcards) {
                    rules.name_globs.push_back(ex);
                } else {
                    rules.names.insert(ex);
                }
                continue;
            }

            // Path-like rules.
            fs::path ex_path(ex);
            if (!has_wildcards) {
                // Exact path.
                try {
                    auto ex_abs = fs::absolute(ex_path).lexically_normal();
                    rules.abs_paths.insert(ex_abs.string());
                } catch (...) {
                    // Ignore invalid paths
                }
                continue;
            }

            // Wildcard path patterns.
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
        }
        return rules;
    }

    bool FileFinder::is_excluded(
        const std::filesystem::path &path,
        const ExcludeRules &rules,
        bool check_components) const {

        const std::string filename = path.filename().string();

        // 1. Check hidden files
        if (rules.ignore_hidden && !filename.empty() && filename[0] == '.') {
            return true;
        }

        // 2. Fast check: Exact Name Match
        if (rules.names.count(filename)) {
            return true;
        }

        // 3. Fast check: Extension Match
        if (!rules.extensions.empty()) {
            std::string ext = path.extension().string();
            if (rules.extensions.count(ext)) {
                return true;
            }
        }

        // 4. Fast check: Name Glob
        for (const auto &pattern : rules.name_globs) {
            if (match_glob(filename, pattern))
                return true;
        }

        // If we need to check components (e.g. for non-recursive file list), do it here
        if (check_components) {
            for (const auto &comp : path) {
                std::string comp_str = comp.string();
                // Skip "." and ".." as they are not real path components
                if (comp_str == "." || comp_str == "..") {
                    continue;
                }
                if (rules.ignore_hidden && !comp_str.empty() && comp_str[0] == '.') {
                    return true;
                }
                if (rules.names.count(comp_str)) {
                    return true;
                }
                for (const auto &pattern : rules.name_globs) {
                    if (match_glob(comp_str, pattern)) {
                        return true;
                    }
                }
            }
        }

        // 5. Path checks (Expensive)
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
            // Match pattern against path suffixes
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

    void FileFinder::generate_default_excludes(
        std::unordered_set<std::string> &names,
        std::unordered_set<std::string> &extensions) const {

        names.insert(default_excludes::default_fullname_excludes.begin(),
                     default_excludes::default_fullname_excludes.end());
        extensions.insert(default_excludes::default_extension_excludes.begin(),
                          default_excludes::default_extension_excludes.end());
    }

    std::vector<std::string> FileFinder::find_files_in_dir(
        const std::string &dir,
        bool recursive,
        const std::unordered_set<std::string> &extensions,
        const ExcludeRules &rules) const {

        using frd_iter = fs::recursive_directory_iterator;
        using fd_iter = fs::directory_iterator;
        std::vector<std::string> files;

        // NOTE: when the shell expands patterns like `./**/` into explicit directories
        // (including excluded ones like `./build`), we should still honor default excludes.
        // If the root directory itself is excluded, skip it entirely.
        if (is_excluded(fs::path(dir), rules, true)) {
            return files;
        }

        auto should_collect = [&](const fs::path &p) {
            if (is_excluded(p, rules, true)) {
                return false;
            }

            const auto path_str = p.string();
            if (contains_rule_file_name(path_str)) {
                return false;
            }

            if (!extensions.empty() && !has_extension(path_str, extensions)) {
                return false;
            }

            return true;
        };

        try {
            if (recursive) {
                std::error_code ec;
                frd_iter it(dir, fs::directory_options::skip_permission_denied, ec);
                frd_iter end;
                for (; !ec && it != end; it.increment(ec)) {
                    const auto &entry = *it;
                    const fs::path p = entry.path();

                    if (is_excluded(p, rules, false)) {
                        std::error_code ec_dir;
                        if (entry.is_directory(ec_dir) && !ec_dir) {
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    std::error_code ec_dir;
                    if (entry.is_directory(ec_dir) && !ec_dir) {
                        continue;
                    }

                    std::error_code ec_file;
                    if (entry.is_regular_file(ec_file) && !ec_file && should_collect(p)) {
                        files.emplace_back(p.string());
                    }
                }

                if (ec) {
                    error("Accessing directory '", dir, "': ", ec.message());
                }
            } else {
                std::error_code ec;
                fd_iter it(dir, ec);
                if (ec) {
                    error("Accessing directory '", dir, "': ", ec.message());
                    return files;
                }

                for (const auto &entry : it) {
                    std::error_code ec_file;
                    if (!entry.is_regular_file(ec_file) || ec_file) {
                        continue;
                    }

                    const fs::path p = entry.path();
                    if (should_collect(p)) {
                        files.emplace_back(p.string());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            error("Accessing directory '", dir, "': ", e.what());
        }

        return files;
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

    std::string FileFinder::maybe_expand_tilde(const std::string &path) const {
        if (path.empty() || path[0] != '~') {
            return path;
        }
        const char *home = std::getenv("HOME");
        if (!home) {
            return path;
        }
        return std::string(home) + path.substr(1);
    }

    std::string FileFinder::strip_trailing_slashes(std::string s) const {
        while (!s.empty() && (s.back() == '/' || s.back() == '\\')) {
            s.pop_back();
        }
        return s;
    }

} // namespace punp

// NOTE: This namespace block is used to impl latex jumping methods separately.
namespace punp {
    std::unordered_set<std::string> FileFinder::extract_latex_includes(const std::string_view &content) const {
        std::unordered_set<std::string> includes;
        size_t pos = 0;

        while (pos < content.size()) {
            // Look for \input{ or \include{
            size_t input_pos = content.find("\\input{", pos);
            size_t include_pos = content.find("\\include{", pos);

            size_t found_pos = std::string::npos;
            size_t cmd_len = 0;

            if (input_pos != std::string::npos && (include_pos == std::string::npos || input_pos < include_pos)) {
                found_pos = input_pos;
                cmd_len = 7; // length of "\input{"
            } else if (include_pos != std::string::npos) {
                found_pos = include_pos;
                cmd_len = 9; // length of "\include{"
            }

            if (found_pos == std::string::npos) {
                break;
            }

            // Find the closing brace
            size_t brace_start = found_pos + cmd_len;
            size_t brace_end = content.find('}', brace_start);

            if (brace_end == std::string::npos) {
                pos = found_pos + cmd_len;
                continue;
            }

            // Extract the filename
            auto filename = content.substr(brace_start, brace_end - brace_start);

            // Trim whitespace
            size_t first = filename.find_first_not_of(" \t\n\r");
            size_t last = filename.find_last_not_of(" \t\n\r");
            if (first != std::string::npos && last != std::string::npos) {
                filename = filename.substr(first, last - first + 1);
            }

            if (!filename.empty()) {
                includes.insert(std::string(filename));
            }

            pos = brace_end + 1;
        }

        return includes;
    }

    void FileFinder::collect_latex_includes(
        const std::string &tex_file,
        const fs::path &root_dir,
        std::unordered_set<std::string> &visited_files,
        std::unordered_set<std::string> &result_files,
        const ExcludeRules &rules) const {

        // Avoid processing the same file twice
        if (visited_files.count(tex_file)) {
            return;
        }
        visited_files.insert(tex_file);

        // Add the current file to results
        result_files.insert(tex_file);

        // Read the file content
        std::ifstream file(tex_file);
        if (!file.is_open()) {
            return;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        // Extract included files
        auto includes = extract_latex_includes(content);

        // Get the directory of the current tex file
        fs::path tex_path(tex_file);
        fs::path tex_dir = tex_path.parent_path();

        // Process each included file
        for (const auto &include : includes) {
            std::string include_path = include;

            // Add .tex extension if not present
            if (include_path.size() < 4 || include_path.substr(include_path.size() - 4) != ".tex") {
                include_path += ".tex";
            }

            fs::path full_path;
            bool found = false;

            // NOTE: LaTeX \input{} paths can be:
            // 1. Relative to the current file's directory
            // 2. Relative to the root document's directory
            // Try both approaches

            if (fs::path(include_path).is_absolute()) {
                // Absolute path
                full_path = include_path;
                found = true;
            } else {
                // Try relative to current file's directory first
                fs::path candidate1 = tex_dir / include_path;

                std::error_code ec;
                fs::path canonical1 = fs::canonical(candidate1, ec);
                if (!ec && is_file(canonical1.string())) {
                    full_path = canonical1;
                    found = true;
                } else {
                    // Try relative to root directory
                    fs::path candidate2 = root_dir / include_path;
                    fs::path canonical2 = fs::canonical(candidate2, ec);
                    if (!ec && is_file(canonical2.string())) {
                        full_path = canonical2;
                        found = true;
                    } else {
                        // If both canonical fail, try lexically_normal
                        if (is_file(candidate1.lexically_normal().string())) {
                            full_path = candidate1.lexically_normal();
                            found = true;
                        } else if (is_file(candidate2.lexically_normal().string())) {
                            full_path = candidate2.lexically_normal();
                            found = true;
                        }
                    }
                }
            }

            if (!found) {
                continue;
            }

            std::string full_path_str = full_path.string();

            // Check if the file is excluded
            if (is_excluded(full_path, rules, true)) {
                continue;
            }

            // Recursively process the included file
            collect_latex_includes(full_path_str, root_dir, visited_files, result_files, rules);
        }
    }

} // namespace punp
