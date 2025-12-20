#pragma once

#include "base/types.h"

#include <memory>
#include <string>
#include <vector>

namespace punp {

    class ConfigManager {
    public:
        explicit ConfigManager()
            : _rep_map_ptr(std::make_shared<ReplacementMap>()),
              _protected_regions_ptr(std::make_shared<ProtectedRegions>()) {}
        ~ConfigManager() = default;

        bool load(const RuleConfig &rule_config, bool verbose = false);

        const std::shared_ptr<ReplacementMap> replacement_map() const noexcept { return _rep_map_ptr; }
        const std::shared_ptr<ProtectedRegions> protected_regions() const noexcept { return _protected_regions_ptr; }
        bool empty() const noexcept { return _rep_map_ptr->empty(); }
        size_t size() const noexcept { return _rep_map_ptr->size(); }

    private:
        std::shared_ptr<ReplacementMap> _rep_map_ptr;
        std::shared_ptr<ProtectedRegions> _protected_regions_ptr;

        std::vector<std::string> find_files(const RuleConfig &rule_config) const;

        bool parse_file(const std::string &file_path);
        bool parse_console_rule(const std::string &console_rule);
        bool parse(const std::string &file_name, const std::string &contents);
    };

} // namespace punp
