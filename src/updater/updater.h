#pragma once

#include "base/types.h"
#include <array>
#include <filesystem>
#include <string>

namespace punp {
    class Updater {
    public:
        Updater() = default;
        ~Updater() = default;

        void maybe_update(const UpdateType &update_type) const;

    private:
        enum class DownloadTool {
            NONE,
            WGET,
            CURL,
        };
        enum class CheckResult {
            FAILED,
            NO_UPDATE,
            UPDATED,
        };
        using version_t = std::array<int, 3>;

        bool command_exists(const std::string &cmd) const;
        DownloadTool detect_download_tool() const;
        std::string get_remote_version(const DownloadTool &tool, const std::filesystem::path &tmp_dir) const;
        version_t parse_version(const std::string &version_str) const;
        CheckResult compare_versions(const std::string &local_version, const std::string &remote_version) const;
        CheckResult check_and_compare(const UpdateType &update_type, const std::filesystem::path &tmp_dir, std::string &latest_version) const;
        void update(const std::filesystem::path &tmp_dir, const UpdateType &update_type, const std::string &latest_version) const;
    };
} // namespace punp
