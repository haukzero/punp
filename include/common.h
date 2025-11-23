#pragma once

#include <string>
#include <thread>

namespace punp {

    namespace Version {
        constexpr const char *VERSION = "3.0.2";
    } // namespace Version

    namespace RuleFile {
        constexpr const char *NAME = ".prules";
    } // namespace RuleFile

    namespace Hardware {
        const size_t HW_MAX_THREADS = std::thread::hardware_concurrency();
        const size_t AUTO_NUM_THREADS = static_cast<size_t>(HW_MAX_THREADS * 1.5);
    } // namespace Hardware

    namespace PageConfig {
        constexpr const size_t SIZE = 16 * 1024; // 16KB per page
    } // namespace PageConfig

    namespace StoreDir {
        inline const char *_HOME = std::getenv("HOME");
        inline const std::string ROOT_DIR = std::string(_HOME) + "/.local";
        inline const std::string CONFIG_DIR = ROOT_DIR + "/share/punp/";
    } // namespace StoreDir

    namespace RemoteStore {
        constexpr const char *repo_url = "https://github.com/haukzero/punp.git";
        constexpr const char *version_file_url = "https://raw.githubusercontent.com/haukzero/punp/refs/heads/master/include/common.h";
    } // namespace RemoteStore

#if defined(__GNUC__) || defined(__clang__)
#define UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define UNREACHABLE() __assume(0)
#else
#include <cstdlib>
    [[noreturn]] inline void unreachable() { std::abort(); }
#define UNREACHABLE() unreachable()
#endif
} // namespace punp
