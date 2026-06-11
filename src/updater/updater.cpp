#include "updater/updater.h"

#include "base/color_print.h"
#include "base/common.h"
#include "base/types.h"
#include "version.h"

#include <filesystem>
#include <fstream>
#include <regex>

namespace punp {
    void Updater::maybe_update(const UpdateType &update_type) const {
        println("Checking for updates...");

        auto tmp_dir = std::filesystem::temp_directory_path() / "punp_updater";
        if (!std::filesystem::exists(tmp_dir)) {
            std::filesystem::create_directory(tmp_dir);
        } else {
            std::filesystem::remove_all(tmp_dir);
            std::filesystem::create_directory(tmp_dir);
        }

        std::string lastest_version;
        auto result = check_and_compare(update_type, tmp_dir, lastest_version);
        if (result == CheckResult::NO_UPDATE) {
            update(tmp_dir, update_type, lastest_version);
        }

        println("Cleaning up temporary files...");
        std::filesystem::remove_all(tmp_dir);
        println("Cleanup complete.");
    }

    bool Updater::command_exists(const std::string &cmd) const {
        std::string check_cmd = "command -v " + cmd + " >/dev/null 2>&1";
        int ret = std::system(check_cmd.c_str());
        return (ret == 0);
    }

    std::string Updater::get_remote_version(const std::filesystem::path &tmp_dir) const {
        // NOTE: The latest stable version is determined by the highest semantic
        // version git tag, which is exactly what `git clone --branch <tag>` uses
        // during the update. Relying on the tags (instead of parsing a branch's
        // CMakeLists.txt) avoids the case where a tag is pushed before the
        // in-source version is bumped, which would otherwise hide a new release.
        auto tmp_file_path = tmp_dir / "remote_tags.txt";
        std::string ls_remote_cmd = "git ls-remote --tags --refs " + std::string(RemoteStore::repo_url) +
                                    " > " + tmp_file_path.string() + " 2>/dev/null";
        if (std::system(ls_remote_cmd.c_str()) != 0) {
            error("Failed to fetch remote tags.");
            return "";
        }

        std::ifstream tags_file(tmp_file_path);
        if (!tags_file.is_open()) {
            error("Failed to open remote tags file.");
            return "";
        }

        std::regex tag_regex(R"(refs/tags/([0-9]+\.[0-9]+\.[0-9]+)\s*$)");
        std::string latest;
        version_t latest_ver = {0, 0, 0};
        std::string line;
        while (std::getline(tags_file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, tag_regex)) {
                std::string candidate = match.str(1);
                version_t candidate_ver = parse_version(candidate);
                if (latest.empty() || candidate_ver > latest_ver) {
                    latest = candidate;
                    latest_ver = candidate_ver;
                }
            }
        }
        tags_file.close();

        if (latest.empty()) {
            error("No valid version tag found in the remote repository.");
            return "";
        }
        return latest;
    }

    Updater::version_t Updater::parse_version(const std::string &version_str) const {
        version_t version = {0, 0, 0};
        size_t start = 0;
        size_t end = version_str.find('.');
        for (size_t i = 0; i < 3; ++i) {
            if (end == std::string::npos && i < 2) {
                version[i] = std::stoi(version_str.substr(start));
                break;
            }
            version[i] = std::stoi(version_str.substr(start, end - start));
            start = end + 1;
            end = version_str.find('.', start);
        }
        return version;
    }

    Updater::CheckResult Updater::compare_versions(const std::string &local_version, const std::string &remote_version) const {
        version_t local_ver = parse_version(local_version);
        version_t remote_ver = parse_version(remote_version);

        // std::array compares lexicographically: major, then minor, then patch.
        if (remote_ver > local_ver) {
            return CheckResult::NO_UPDATE;
        }
        println_green("You are using the latest version (", local_version, ").");
        return CheckResult::UPDATED;
    }

    Updater::CheckResult Updater::check_and_compare(const UpdateType &update_type, const std::filesystem::path &tmp_dir, std::string &latest_version) const {
        // NOTE: For nightly updates, we always proceed to update.
        if (update_type == UpdateType::NIGHTLY) {
            return CheckResult::NO_UPDATE;
        }

        if (!command_exists("git")) {
            error("Git is not installed. Please install Git to check for updates.");
            return CheckResult::FAILED;
        }

        std::string remote_version_str = get_remote_version(tmp_dir);
        if (remote_version_str.empty()) {
            return CheckResult::FAILED;
        }
        latest_version = remote_version_str;
        return compare_versions(punp::version, remote_version_str);
    }

    void Updater::update(const std::filesystem::path &tmp_dir, const UpdateType &update_type, const std::string &latest_version) const {
        if (!command_exists("git")) {
            error("Git is not installed. Please install Git to update punp.");
            return;
        }

        if (!command_exists("cmake")) {
            error("CMake is not installed. Please install CMake to update punp.");
            return;
        }

        println_yellow("Updating punp to the latest version...");

        auto clone_path = tmp_dir / "punp_repo";
        std::string clone_cmd;
        switch (update_type) {
        case UpdateType::STABLE:
            clone_cmd = "git clone --depth 1 --branch " + latest_version + " " + std::string(RemoteStore::repo_url) + " " + clone_path.string();
            break;
        case UpdateType::NIGHTLY:
            clone_cmd = "git clone --depth 1 " + std::string(RemoteStore::repo_url) + " " + clone_path.string();
            break;
        default:
            UNREACHABLE();
        }
        if (std::system(clone_cmd.c_str()) != 0) {
            error("Failed to clone the repository.");
            return;
        }

        auto build_path = clone_path / "build";
        std::string cmake_conf_cmd = "cmake -S " + clone_path.string() + " -B " + build_path.string() + " -DCMAKE_BUILD_TYPE=Release";
        if (std::system(cmake_conf_cmd.c_str()) != 0) {
            error("CMake configuration failed.");
            return;
        }

        std::string build_cmd = "cmake --build " + build_path.string();
        if (std::system(build_cmd.c_str()) != 0) {
            error("Build failed.");
            return;
        }

        std::string install_cmd = "cmake --install " + build_path.string();
        if (std::system(install_cmd.c_str()) != 0) {
            error("Installation failed.");
            return;
        }

        println_green("punp has been successfully updated to the latest version!");
    }
} // namespace punp
