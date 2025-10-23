#pragma once

#include <string>

namespace punp {

    namespace Version {
        constexpr const char *VERSION = "2.2.4";
    } // namespace Version

    namespace RuleFile {
        constexpr const char *NAME = ".prules";
    } // namespace RuleFile

    namespace Colors {
        constexpr const char *RESET = "\033[0m";
        constexpr const char *RED = "\033[31m";
        constexpr const char *GREEN = "\033[32m";
        constexpr const char *YELLOW = "\033[33m";
        constexpr const char *BLUE = "\033[34m";
        constexpr const char *MAGENTA = "\033[35m";
        constexpr const char *CYAN = "\033[36m";
    }

    namespace PageConfig {
        constexpr const size_t SIZE = 16 * 1024; // 16KB per page
    } // namespace PageConfig

    namespace StoreDir {
        inline const char *_HOME = std::getenv("HOME");
        inline const std::string ROOT_DIR = std::string(_HOME) + "/.local";
        inline const std::string CONFIG_DIR = ROOT_DIR + "/share/punp/";
    } // namespace StoreDir

} // namespace punp
