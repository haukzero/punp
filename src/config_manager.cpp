#include "config_manager.h"
#include "common.h"
#include "types.h"
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
        size_t arrow_pos = line.find(" -> ");
        if (arrow_pos == std::string::npos) {
            return false;
        }

        // Extract from and to parts
        std::string from = line.substr(0, arrow_pos);
        std::string to = line.substr(arrow_pos + 4);

        // Remove quotes and extract content
        if (from.length() < 2 || from.front() != '"' || from.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid 'from' format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        if (to.length() < 2 || to.front() != '"' || to.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid 'to' format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        std::string from_str = from.substr(1, from.length() - 2);
        std::string to_str = to.substr(1, to.length() - 2);

        // If from == to, skip the rule
        if (from_str == to_str) {
            std::cerr << Colors::YELLOW << "Warning: Skipping rule with identical 'from' and 'to' at "
                      << file_path << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }

        // Convert to wide strings
        text_t wf = to_tstr(from_str);
        text_t wt = to_tstr(to_str);

        // Add or update rule (later rules override earlier ones)
        _rep_map[wf] = wt;
        return true;
    }

    bool ConfigManager::parse_erase_rule(const std::string &file_path, const int lno, const std::string &line) {
        // Erase format: - "from"
        if (line.compare(0, 2, "- ") != 0) {
            return false;
        }
        std::string content = line.substr(2);
        if (content.empty() || content[0] != '"' || content.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid format at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }
        content.erase(0, 1);                 // Remove leading quote
        content.erase(content.length() - 1); // Remove trailing quote
        if (_rep_map.erase(to_tstr(content)) == 0) {
            std::cerr << Colors::YELLOW << "Warning: No rule found to erase at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
        }
        return true;
    }

    bool ConfigManager::parse_protected_region(const std::string &file_path, const int lno, const std::string &line) {
        // Protected region format: !"start" ~ "end"
        if (line.compare(0, 2, "!\"") != 0) {
            return false;
        }
        size_t tilde_pos = line.find(" ~ ");
        if (tilde_pos == std::string::npos) {
            std::cerr << Colors::YELLOW << "Warning: Invalid protected region format at " << file_path
                      << ":" << lno << ": " << line << '\n'
                      << Colors::RESET;
            return false;
        }

        // Extract start and end markers
        std::string start_marker = line.substr(1, tilde_pos - 1);
        std::string end_marker = line.substr(tilde_pos + 3);

        // Remove quotes
        if (start_marker.length() < 2 || start_marker.front() != '"' || start_marker.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid start marker format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        if (end_marker.length() < 2 || end_marker.front() != '"' || end_marker.back() != '"') {
            std::cerr << Colors::YELLOW << "Warning: Invalid end marker format at " << file_path
                      << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        std::string start_str = start_marker.substr(1, start_marker.length() - 2);
        std::string end_str = end_marker.substr(1, end_marker.length() - 2);

        // Convert to wide strings and add to protected regions
        text_t wstart = to_tstr(start_str);
        text_t wend = to_tstr(end_str);
        _protected_regions.emplace_back(wstart, wend);
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
