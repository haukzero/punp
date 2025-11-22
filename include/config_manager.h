#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace punp {

    class ConfigManager {
    private:
        ReplacementMap _rep_map;
        ProtectedRegions _protected_regions;

        void process_statement(const std::string &stmt, const std::string &file_path, int lno);
        bool parse_replace(const std::string &args, const std::string &file_path, int lno);
        bool parse_del(const std::string &args, const std::string &file_path, int lno);
        bool parse_protect(const std::string &args, const std::string &file_path, int lno);
        bool parse_clear();

        std::vector<std::string> find_files() const;
        bool parse(const std::string &file_path);

        void to_upper(std::string &str) const;
        text_t to_tstr(const std::string &str) const;

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
