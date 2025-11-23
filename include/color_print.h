#pragma once

#include <iostream>
#include <sstream>
#include <utility>

namespace punp {

    namespace Colors {
        constexpr const char *RESET = "\033[0m";
        constexpr const char *RED = "\033[31m";
        constexpr const char *GREEN = "\033[32m";
        constexpr const char *YELLOW = "\033[33m";
        constexpr const char *BLUE = "\033[34m";
        constexpr const char *MAGENTA = "\033[35m";
        constexpr const char *CYAN = "\033[36m";
    }

    template <typename... Args>
    inline void colored_print(const char *color_code, Args &&...args) {
        std::ostringstream oss;
        (oss << ... << args);
        std::cout << color_code << oss.str() << Colors::RESET;
    }

    template <typename... Args>
    inline void colored_println(const char *color_code, Args &&...args) {
        std::ostringstream oss;
        (oss << ... << args);
        std::cout << color_code << oss.str() << '\n'
                  << Colors::RESET;
    }

#define PUNP_DEFINE_COLOR_PRINT(FUNC_NAME, COLOR_CONST)          \
    template <typename... Args>                                  \
    inline void FUNC_NAME(Args &&...args) {                      \
        colored_print(COLOR_CONST, std::forward<Args>(args)...); \
    }

    // Define print helpers for all supported colors
    PUNP_DEFINE_COLOR_PRINT(print, Colors::RESET)
    PUNP_DEFINE_COLOR_PRINT(print_red, Colors::RED)
    PUNP_DEFINE_COLOR_PRINT(print_green, Colors::GREEN)
    PUNP_DEFINE_COLOR_PRINT(print_yellow, Colors::YELLOW)
    PUNP_DEFINE_COLOR_PRINT(print_blue, Colors::BLUE)
    PUNP_DEFINE_COLOR_PRINT(print_magenta, Colors::MAGENTA)
    PUNP_DEFINE_COLOR_PRINT(print_cyan, Colors::CYAN)

#undef PUNP_DEFINE_COLOR_PRINT

#define PUNP_DEFINE_COLOR_PRINTLN(FUNC_NAME, COLOR_CONST)          \
    template <typename... Args>                                    \
    inline void FUNC_NAME(Args &&...args) {                        \
        colored_println(COLOR_CONST, std::forward<Args>(args)...); \
    }

    // Define println helpers for all supported colors
    PUNP_DEFINE_COLOR_PRINTLN(println, Colors::RESET)
    PUNP_DEFINE_COLOR_PRINTLN(println_red, Colors::RED)
    PUNP_DEFINE_COLOR_PRINTLN(println_green, Colors::GREEN)
    PUNP_DEFINE_COLOR_PRINTLN(println_yellow, Colors::YELLOW)
    PUNP_DEFINE_COLOR_PRINTLN(println_blue, Colors::BLUE)
    PUNP_DEFINE_COLOR_PRINTLN(println_magenta, Colors::MAGENTA)
    PUNP_DEFINE_COLOR_PRINTLN(println_cyan, Colors::CYAN)

#undef PUNP_DEFINE_COLOR_PRINTLN

    template <typename... Args>
    inline void warn(Args &&...args) {
        std::ostringstream oss;
        (oss << ... << args);
        std::cerr << Colors::YELLOW << "Warn: " << oss.str() << '\n'
                  << Colors::RESET;
    }

    template <typename... Args>
    inline void error(Args &&...args) {
        std::ostringstream oss;
        (oss << ... << args);
        std::cerr << Colors::RED << "Error: " << oss.str() << '\n'
                  << Colors::RESET;
    }

} // namespace punp
