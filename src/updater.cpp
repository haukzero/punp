#include "updater.h"
#include "common.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace punp {
    void Updater::maybe_update() {
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

        std::cout << "Cleaning up temporary files...\n";
        std::filesystem::remove_all(tmp_dir);
        std::cout << "Cleanup complete.\n";
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
        auto tmp_file_path = tmp_dir / "punp_version.h";
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
            std::cerr << Colors::RED << "Error: Failed to download version file."
                      << Colors::RESET << "\n";
            return "";
        }

        std::ifstream version_file(tmp_file_path);
        if (!version_file.is_open()) {
            std::cerr << Colors::RED << "Error: Failed to open downloaded version file."
                      << Colors::RESET << "\n";
            return "";
        }
        std::string content((std::istreambuf_iterator<char>(version_file)), std::istreambuf_iterator<char>());
        version_file.close();

        std::string version_prefix = "constexpr const char *VERSION = \"";
        size_t pos = content.find(version_prefix);
        if (pos != std::string::npos) {
            pos += version_prefix.length();
            size_t end_pos = content.find('"', pos);
            if (end_pos != std::string::npos) {
                return content.substr(pos, end_pos - pos);
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
        std::cout << Colors::GREEN << "You are using the latest version (" << local_version << ").\n"
                  << Colors::RESET;
        return CheckResult::UPDATED;
    }

    Updater::CheckResult Updater::check_and_compare(const std::filesystem::path &tmp_dir) const {
        DownloadTool tool = detect_download_tool();
        if (tool == DownloadTool::NONE) {
            std::cerr << Colors::RED << "Error: No download tool found.\n"
                      << Colors::RESET;
            std::cerr << Colors::YELLOW << "Hint: You can try downloading the downloader first and then try again:\n"
                      << "  - wget\n"
                      << "  - curl\n"
                      << Colors::RESET;
            return CheckResult::FAILED;
        }

        std::string remote_version_str = get_remote_version(tool, tmp_dir);
        if (remote_version_str.empty()) {
            return CheckResult::FAILED;
        }
        return compare_versions(Version::VERSION, remote_version_str);
    }

    void Updater::update(const std::filesystem::path &tmp_dir) const {
        if (!command_exists("git")) {
            std::cerr << Colors::RED << "Error: Git is not installed. Please install Git to update punp.\n"
                      << Colors::RESET;
            return;
        }

        if (!command_exists("cmake")) {
            std::cerr << Colors::RED << "Error: CMake is not installed. Please install CMake to update punp.\n"
                      << Colors::RESET;
            return;
        }

        std::cout << Colors::YELLOW << "Updating punp to the latest version..." << Colors::RESET << "\n";

        auto clone_path = tmp_dir / "punp_repo";
        std::string clone_cmd = "git clone --depth 1 " + std::string(RemoteStore::repo_url) + " " + clone_path.string();
        if (std::system(clone_cmd.c_str()) != 0) {
            std::cerr << Colors::RED << "Error: Failed to clone the repository.\n"
                      << Colors::RESET;
            return;
        }

        auto build_path = clone_path / "build";
        std::string cmake_conf_cmd = "cmake -S " + clone_path.string() + " -B " + build_path.string() + " -DCMAKE_BUILD_TYPE=MinSizeRel";
        if (std::system(cmake_conf_cmd.c_str()) != 0) {
            std::cerr << Colors::RED << "Error: CMake configuration failed.\n"
                      << Colors::RESET;
            return;
        }

        std::string build_cmd = "cmake --build " + build_path.string();
        if (std::system(build_cmd.c_str()) != 0) {
            std::cerr << Colors::RED << "Error: Build failed.\n"
                      << Colors::RESET;
            return;
        }

        std::string install_cmd = "cmake --install " + build_path.string();
        if (std::system(install_cmd.c_str()) != 0) {
            std::cerr << Colors::RED << "Error: Installation failed.\n"
                      << Colors::RESET;
            return;
        }

        std::cout << Colors::GREEN << "punp has been successfully updated to the latest version!\n"
                  << Colors::RESET;
    }
} // namespace punp
