#include "config_manager.h"
#include "color_print.h"
#include "common.h"
#include "config_parser/parser.h"
#include <cstdlib>
#include <fstream>
#include <vector>

namespace punp {

    bool ConfigManager::load(bool verbose) {
        auto config_files = find_files();

        if (config_files.empty()) {
            error("No configuration files found.");
            println("Please create a '", RuleFile::NAME, "' file in:");
            println("  - Current directory, or");
            println("  - User config directory (", StoreDir::CONFIG_DIR, ")");
            return false;
        }

        bool ok = false;
        for (const auto &cf : config_files) {
            if (parse(cf)) {
                if (verbose) {
                    println("Loaded config from: ", cf);
                }
                ok = true;
            } else if (verbose) {
                warn("Skipped config file: ", cf, " (not found or invalid)");
            }
        }

        if (verbose && ok) {
            println("Total replacement rules loaded: ", _rep_map_ptr->size());
            println("Total protected rules loaded: ", _protected_regions_ptr->size());
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
