#include "config_manager.h"
#include "common.h"
#include "types.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace punp {

    bool ConfigManager::load(bool verbose) {
        auto config_files = find_files();

        if (config_files.empty()) {
            std::cerr << Colors::RED << "Error: No configuration files found.\n";
            std::cerr << "Please create a '" << RuleFile::NAME << "' file in:\n";
            std::cerr << "  - Current directory, or\n";
            std::cerr << "  - User config directory (" << StoreDir::CONFIG_DIR << ")\n"
                      << Colors::RESET;
            return false;
        }

        bool ok = false;
        for (const auto &cf : config_files) {
            if (parse(cf)) {
                if (verbose) {
                    std::cout << "Loaded config from: " << cf << '\n';
                }
                ok = true;
            } else if (verbose) {
                std::cout << Colors::YELLOW << "Skipped config file: " << cf << " (not found or invalid)" << '\n'
                          << Colors::RESET;
            }
        }

        if (verbose && ok) {
            std::cout << "Total replacement rules loaded: " << _rep_map.size() << '\n';
            std::cout << "Total protected rules loaded: " << _protected_regions.size() << '\n';
        }

        return ok;
    }

    std::vector<std::string> ConfigManager::find_files() const {
        std::vector<std::string> config_files;

        // Check user config directory first (lower priority)
        config_files.emplace_back(StoreDir::CONFIG_DIR + RuleFile::NAME);

        // Check current directory (higher priority)
        config_files.emplace_back(RuleFile::NAME);

        return config_files;
    }

    bool ConfigManager::parse(const std::string &file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        int n_line = 0;
        size_t n_rule = 0;

        while (std::getline(file, line)) {
            n_line++;

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Trim leading whitespace
            line.erase(0, line.find_first_not_of(" \t"));

            bool parsed = parse_add_rule(file_path, n_line, line) ||
                          parse_erase_rule(file_path, n_line, line) ||
                          parse_protected_region(file_path, n_line, line) ||
                          parse_clear_rules(line);
            if (parsed) {
                n_rule++;
            } else {
                std::cerr << Colors::YELLOW << "Warning: Unrecognized rule at " << file_path
                          << ":" << n_line << ": " << line << '\n'
                          << Colors::RESET;
            }
        }

        return n_rule > 0;
    }

    bool ConfigManager::parse_clear_rules(const std::string &line) {
        if (line != "--") {
            return false;
        }
        _rep_map.clear(); // Clear all rules
        return true;
    }

    bool ConfigManager::parse_add_rule(const std::string &file_path, const int lno, const std::string &line) {
        // Add format: "from" -> "to"
        std::string_view line_view(line);
        size_t arrow_pos = line_view.find(" -> ");
        if (arrow_pos == std::string_view::npos) {
            return false;
        }

        // Extract from and to parts
        std::string_view from_view = line_view.substr(0, arrow_pos);
        std::string_view to_view = line_view.substr(arrow_pos + 4);

        // Remove quotes and extract content
        if (from_view.length() < 2 || from_view.front() != '"' || from_view.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid 'from' format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        if (to_view.length() < 2 || to_view.front() != '"' || to_view.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid 'to' format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        std::string_view from_str_view = from_view.substr(1, from_view.length() - 2);
        std::string_view to_str_view = to_view.substr(1, to_view.length() - 2);

        // If from == to, skip the rule
        if (from_str_view == to_str_view) {
            std::cerr << Colors::YELLOW << "Warning: Skipping rule with identical 'from' and 'to' at "
                      << file_path << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }

        // Convert to wide strings
        text_t wf = to_tstr(std::string(from_str_view));
        text_t wt = to_tstr(std::string(to_str_view));

        // Add or update rule (later rules override earlier ones)
        _rep_map[wf] = wt;
        return true;
    }

    bool ConfigManager::parse_erase_rule(const std::string &file_path, const int lno, const std::string &line) {
        // Erase format: - "from"
        std::string_view line_view(line);
        if (line_view.substr(0, 2) != "- ") {
            return false;
        }
        std::string_view content_view = line_view.substr(2);
        if (content_view.empty() || content_view[0] != '"' || content_view.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid format at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }
        std::string_view inner_view = content_view.substr(1, content_view.length() - 2);
        if (_rep_map.erase(to_tstr(std::string(inner_view))) == 0) {
            std::cerr << Colors::YELLOW << "Warning: No rule found to erase at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
        }
        return true;
    }

    bool ConfigManager::parse_protected_region(const std::string &file_path, const int lno, const std::string &line) {
        // Protected region format: !"start" ~ "end"
        std::string_view line_view(line);
        if (line_view.substr(0, 2) != "!\"") {
            return false;
        }
        size_t tilde_pos = line_view.find(" ~ ");
        if (tilde_pos == std::string_view::npos) {
            std::cerr << Colors::YELLOW << "Warning: Invalid protected region format at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }

        // Extract start and end markers
        std::string_view start_marker_view = line_view.substr(1, tilde_pos - 1);
        std::string_view end_marker_view = line_view.substr(tilde_pos + 3);

        // Remove quotes
        if (start_marker_view.length() < 2 || start_marker_view.front() != '"' || start_marker_view.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid start marker format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        if (end_marker_view.length() < 2 || end_marker_view.front() != '"' || end_marker_view.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid end marker format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        std::string_view start_str_view = start_marker_view.substr(1, start_marker_view.length() - 2);
        std::string_view end_str_view = end_marker_view.substr(1, end_marker_view.length() - 2);

        // Convert to wide strings and add to protected regions
        text_t wstart = to_tstr(std::string(start_str_view));
        text_t wend = to_tstr(std::string(end_str_view));
        if (wstart.empty() || wend.empty()) {
            std::cerr << Colors::YELLOW << "Warning: Empty start or end marker at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }
        _protected_regions.emplace_back(wstart, wend);

        // NOTE: Now sort protected regions by start marker length (shorter first).
        // It can be changed to priority-based if needed.
        if (_protected_regions.size() > 1) {
            std::sort(_protected_regions.begin(), _protected_regions.end(),
                      [](const ProtectedRegion &a, const ProtectedRegion &b) {
                          return a.first.length() < b.first.length();
                      });
        }

        return true;
    }

    text_t ConfigManager::to_tstr(const std::string &str) const {
        convert_t converter;
        try {
            return converter.from_bytes(str);
        } catch (const std::exception &) {
            // Fallback for invalid UTF-8
            text_t result;
            result.reserve(str.length());
            for (char c : str) {
                result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
            }
            return result;
        }
    }

} // namespace punp
