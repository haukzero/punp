#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace punp {

    class ConfigManager {
    private:
        ReplacementMap _rep_map;
        ProtectedRegions _protected_regions;

        bool parse_clear_rules(const std::string &line);
        bool parse_add_rule(const std::string &file_path, const int lno, const std::string &line);
        bool parse_erase_rule(const std::string &file_path, const int lno, const std::string &line);
        bool parse_protected_region(const std::string &file_path, const int lno, const std::string &line);

        std::vector<std::string> find_files() const;
        bool parse(const std::string &file_path);
        std::wstring to_wstr(const std::string &str) const;

    public:
        ConfigManager() = default;
        ~ConfigManager() = default;

        bool load(bool verbose = false);

        const ReplacementMap &replacement_map() const noexcept { return _rep_map; }
        const ProtectedRegions &protected_regions() const noexcept { return _protected_regions; }
        bool empty() const noexcept { return _rep_map.empty(); }
        size_t size() const noexcept { return _rep_map.size(); }
    };

} // namespace punp
