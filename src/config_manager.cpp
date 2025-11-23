#include "config_manager.h"
#include "common.h"
#include "config_parser/parser.h"
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
            std::cout << "Total replacement rules loaded: " << _rep_map_ptr->size() << '\n';
            std::cout << "Total protected rules loaded: " << _protected_regions_ptr->size() << '\n';
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

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t rules_count_before = _rep_map_ptr->size() + _protected_regions_ptr->size();

        config_parser::Parser parser(file_path, content, _rep_map_ptr, _protected_regions_ptr);
        parser.parse();

        return (_rep_map_ptr->size() + _protected_regions_ptr->size()) > rules_count_before || rules_count_before > 0;
    }

} // namespace punp
