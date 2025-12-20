#pragma once

#include <string>
#include <thread>

namespace punp {

    namespace RuleFile {
        constexpr const char *NAME = ".prules";
        const std::string GLOBAL_RULE_FILE_DIR = std::string(std::getenv("HOME")) + "/.local/share/punp";
        const std::string GLOBAL_RULE_FILE_PATH = GLOBAL_RULE_FILE_DIR + "/" + NAME;
    } // namespace RuleFile

    namespace Hardware {
        const size_t HW_MAX_THREADS = std::thread::hardware_concurrency();
        const size_t AUTO_NUM_THREADS = static_cast<size_t>(HW_MAX_THREADS * 1.5);
    } // namespace Hardware

    namespace PageConfig {
        constexpr const size_t SIZE = 16 * 1024; // 16KB per page
    } // namespace PageConfig

    namespace RemoteStore {
        constexpr const char *repo_url = "https://github.com/haukzero/punp.git";
        constexpr const char *version_file_url = "https://raw.githubusercontent.com/haukzero/punp/refs/heads/master/CMakeLists.txt";
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
