#include "config/config_manager.h"

#include "base/color_print.h"
#include "base/common.h"
#include "config/parser/parser.h"

#include <cstdlib>
#include <fstream>
#include <vector>

namespace punp {

    bool ConfigManager::load(const RuleConfig &rule_config, bool verbose) {
        auto config_files = find_files(rule_config);

        if (config_files.empty() && rule_config.console_rule.empty()) {
            error("No configuration files found.");
            println("Please create a '", RuleFile::NAME, "' file in:");
            println("  - Current directory, or");
            println("  - User config directory (", RuleFile::GLOBAL_RULE_FILE_DIR, ")");
            println("Or use --console to specify rules directly, or --rule-file to specify a custom rule file.");
            return false;
        }

        bool ok = false;
        for (const auto &cf : config_files) {
            if (parse_file(cf)) {
                if (verbose) {
                    println("Loaded config from: ", cf);
                }
                ok = true;
            } else if (verbose) {
                warn("Skipped config file: ", cf, " (not found or invalid)");
            }
        }

        if (!rule_config.console_rule.empty()) {
            if (parse_console_rule(rule_config.console_rule)) {
                if (verbose) {
                    println("Loaded rules from command line");
                }
                ok = true;
            } else {
                error("Failed to parse console rule");
                return false;
            }
        }

        if (verbose && ok) {
            println("Total replacement rules loaded: ", _rep_map_ptr->size());
            println("Total protected rules loaded: ", _protected_regions_ptr->size());
        }

        return ok;
    }

    std::vector<std::string> ConfigManager::find_files(const RuleConfig &rule_config) const {
        std::vector<std::string> config_files;

        if (!rule_config.ignore_global_rule_file) {
            config_files.emplace_back(RuleFile::GLOBAL_RULE_FILE_PATH);
        }

        if (!rule_config.rule_file_path.empty()) {
            config_files.emplace_back(rule_config.rule_file_path);
        } else {
            config_files.emplace_back(RuleFile::NAME);
        }

        return config_files;
    }

    bool ConfigManager::parse(const std::string &file_name, const std::string &contents) {
        size_t rules_count_before = _rep_map_ptr->size() + _protected_regions_ptr->size();

        config_parser::Parser parser(file_name, contents, _rep_map_ptr, _protected_regions_ptr);
        parser.parse();

        return (_rep_map_ptr->size() + _protected_regions_ptr->size()) > rules_count_before || rules_count_before > 0;
    }

    bool ConfigManager::parse_file(const std::string &file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return parse(file_path, content);
    }

    bool ConfigManager::parse_console_rule(const std::string &console_rule) {
        return parse("<console>", console_rule);
    }

} // namespace punp
