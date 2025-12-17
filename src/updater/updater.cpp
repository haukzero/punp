#include "updater/updater.h"

#include "base/color_print.h"
#include "base/common.h"
#include "version.h"

#include <filesystem>
#include <fstream>
#include <regex>

namespace punp {
    void Updater::maybe_update() {
        println("Checking for updates...");

        auto tmp_dir = std::filesystem::temp_directory_path() / "punp_updater";
        if (!std::filesystem::exists(tmp_dir)) {
            std::filesystem::create_directory(tmp_dir);
        } else {
            std::filesystem::remove_all(tmp_dir);
            std::filesystem::create_directory(tmp_dir);
        }
        auto result = check_and_compare(tmp_dir);
        if (result == CheckResult::NO_UPDATE) {
            update(tmp_dir);
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

    Updater::DownloadTool Updater::detect_download_tool() const {
        if (command_exists("wget")) {
            return DownloadTool::WGET;
        } else if (command_exists("curl")) {
            return DownloadTool::CURL;
        }
        return DownloadTool::NONE;
    }

    std::string Updater::get_remote_version(const DownloadTool &tool, const std::filesystem::path &tmp_dir) const {
        auto tmp_file_path = tmp_dir / "CMakeLists.txt";
        std::string download_cmd;

        switch (tool) {
        case DownloadTool::WGET:
            download_cmd = "wget -q -O " + tmp_file_path.string() + " " + RemoteStore::version_file_url;
            break;
        case DownloadTool::CURL:
            download_cmd = "curl -s -o " + tmp_file_path.string() + " " + RemoteStore::version_file_url;
            break;
        default:
            UNREACHABLE();
        }

        if (std::system(download_cmd.c_str()) != 0) {
            error("Failed to download version file.");
            return "";
        }

        std::ifstream version_file(tmp_file_path);
        if (!version_file.is_open()) {
            error("Failed to open downloaded version file.");
            return "";
        }
        std::string content((std::istreambuf_iterator<char>(version_file)), std::istreambuf_iterator<char>());
        version_file.close();

        std::string regex_str = "project\\s*\\(\\s*" + std::string(punp::name) + "\\s+VERSION\\s+([0-9.]+)";
        std::regex version_regex(regex_str);
        std::smatch match;
        if (std::regex_search(content, match, version_regex)) {
            if (match.size() > 1) {
                return match.str(1);
            }
        }

        UNREACHABLE();
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

        for (size_t i = 0; i < 3; ++i) {
            if (remote_ver[i] > local_ver[i]) {
                return CheckResult::NO_UPDATE;
            }
        }
        println_green("You are using the latest version (", local_version, ").");
        return CheckResult::UPDATED;
    }

    Updater::CheckResult Updater::check_and_compare(const std::filesystem::path &tmp_dir) const {
        DownloadTool tool = detect_download_tool();
        if (tool == DownloadTool::NONE) {
            error("No download tool found.");
            println_yellow("Hint: You can try downloading the downloader first and then try again:");
            println_yellow("  - wget");
            println_yellow("  - curl");
            return CheckResult::FAILED;
        }

        std::string remote_version_str = get_remote_version(tool, tmp_dir);
        if (remote_version_str.empty()) {
            return CheckResult::FAILED;
        }
        return compare_versions(punp::version, remote_version_str);
    }

    void Updater::update(const std::filesystem::path &tmp_dir) const {
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
        std::string clone_cmd = "git clone --depth 1 " + std::string(RemoteStore::repo_url) + " " + clone_path.string();
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
