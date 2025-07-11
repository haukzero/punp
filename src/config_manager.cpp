#include "config_manager.h"
#include "common.h"
#include <codecvt>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <locale>
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

            // Erase format: - "from"
            line.erase(0, line.find_first_not_of(" \t"));
            if (line.compare(0, 2, "- ") == 0) {
                line.erase(0, 2);
                if (line.empty() || line[0] != '"' || line.back() != '"') {
                    std::cerr << Colors::YELLOW << "Warning: Invalid format at " << file_path
                              << ":" << n_line << ": " << line << '\n'
                              << Colors::RESET;
                    continue;
                }
                line.erase(0, 1);              // Remove leading quote
                line.erase(line.length() - 1); // Remove trailing quote
                if (_rep_map.erase(to_wstr(line)) == 0) {
                    std::cerr << Colors::YELLOW << "Warning: No rule found to erase at " << file_path
                              << ":" << n_line << ": " << line << '\n'
                              << Colors::RESET;
                }
                n_rule++;
                continue;
            }

            // Clear format: "--"
            if (line.compare("--") == 0) {
                _rep_map.clear(); // Clear all rules
                n_rule++;
                continue;
            }

            // Add format: "from" -> "to"
            size_t arrow_pos = line.find(" -> ");
            if (arrow_pos == std::string::npos) {
                std::cerr << Colors::YELLOW << "Warning: Invalid format at " << file_path
                          << ":" << n_line << ": " << line << '\n'
                          << Colors::RESET;
                continue;
            }

            // Extract from and to parts
            std::string from = line.substr(0, arrow_pos);
            std::string to = line.substr(arrow_pos + 4);

            // Remove quotes and extract content
            if (from.length() < 2 || from.front() != '"' || from.back() != '"') {
                std::cerr << Colors::YELLOW << "Warning: Invalid 'from' format at " << file_path
                          << ":" << n_line << '\n'
                          << Colors::RESET;
                continue;
            }

            if (to.length() < 2 || to.front() != '"' || to.back() != '"') {
                std::cerr << Colors::YELLOW << "Warning: Invalid 'to' format at " << file_path
                          << ":" << n_line << '\n'
                          << Colors::RESET;
                continue;
            }

            std::string from_str = from.substr(1, from.length() - 2);
            std::string to_str = to.substr(1, to.length() - 2);

            // If from == to, skip the rule
            if (from_str == to_str) {
                std::cerr << Colors::YELLOW << "Warning: Skipping rule with identical 'from' and 'to' at "
                          << file_path << ":" << n_line << ": " << line << '\n'
                          << Colors::RESET;
                continue;
            }

            // Convert to wide strings
            std::wstring wf = to_wstr(from_str);
            std::wstring wt = to_wstr(to_str);

            // Add or update rule (later rules override earlier ones)
            _rep_map[wf] = wt;
            n_rule++;
        }

        return n_rule > 0;
    }

    std::wstring ConfigManager::to_wstr(const std::string &str) const {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        try {
            return converter.from_bytes(str);
        } catch (const std::exception &) {
            // Fallback for invalid UTF-8
            std::wstring result;
            result.reserve(str.length());
            for (char c : str) {
                result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
            }
            return result;
        }
    }

} // namespace punp
