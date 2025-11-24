#pragma once

#include <iostream>
#include <string_view>
#include <utility>

namespace punp {

    namespace Colors {
        inline constexpr std::string_view RESET = "\033[0m";
        inline constexpr std::string_view RED = "\033[31m";
        inline constexpr std::string_view GREEN = "\033[32m";
        inline constexpr std::string_view YELLOW = "\033[33m";
        inline constexpr std::string_view BLUE = "\033[34m";
        inline constexpr std::string_view MAGENTA = "\033[35m";
        inline constexpr std::string_view CYAN = "\033[36m";
    }

    template <typename... Args>
    inline void colored_print(std::string_view color_code, Args &&...args) {
        std::cout << color_code;
        (std::cout << ... << std::forward<Args>(args));
        std::cout << Colors::RESET;
    }

    template <typename... Args>
    inline void colored_println(std::string_view color_code, Args &&...args) {
        std::cout << color_code;
        (std::cout << ... << std::forward<Args>(args));
        std::cout << Colors::RESET << '\n';
    }

    template <typename... Args>
    inline void colored_print_err(std::string_view color_code, Args &&...args) {
        std::cerr << color_code;
        (std::cerr << ... << std::forward<Args>(args));
        std::cerr << Colors::RESET;
    }

    template <typename... Args>
    inline void colored_println_err(std::string_view color_code, Args &&...args) {
        std::cerr << color_code;
        (std::cerr << ... << std::forward<Args>(args));
        std::cerr << Colors::RESET << '\n';
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
        colored_println_err(Colors::YELLOW, "Warn: ", std::forward<Args>(args)...);
    }

    template <typename... Args>
    inline void error(Args &&...args) {
        colored_println_err(Colors::RED, "Error: ", std::forward<Args>(args)...);
    }

} // namespace punp
