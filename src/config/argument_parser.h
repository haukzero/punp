#pragma once

#include "base/types.h"

#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace punp {

    class ArgumentParser {
    public:
        ArgumentParser() = default;
        ~ArgumentParser() = default;

        const std::vector<std::string> &inputs() const noexcept { return _inputs; }
        const ProcessingConfig &config() const noexcept { return _config; }
        bool show_version() const noexcept { return _show_version; }
        bool show_help() const noexcept { return _show_help; }
        bool show_example() const noexcept { return _show_example; }
        bool update() const noexcept { return _update; }

        bool parse(int argc, char *argv[]);
        static void display_version();
        static void display_help(const std::string &name);
        static void display_example(const std::string &name);

    private:
        int process_args(const std::string &arg, const char *next_arg);
        std::vector<std::string> split_with_commas(const std::string &s) const;

    private:
        // store arg parse results
        std::vector<std::string> _inputs;
        ProcessingConfig _config;
        bool _show_version = false;
        bool _show_help = false;
        bool _show_example = false;
        bool _update = false;

    private:
        // handlers
        using arg_handler_t = int (ArgumentParser::*)(const char *next_arg);
        using arg_handler_map_t = std::unordered_map<std::string, arg_handler_t>;

#define PUNP_ADD_ARG_HANDLER(ARG_SHORT_NAME, ARG_LONG_NAME, HANDLER_FUNC) \
    {ARG_SHORT_NAME, &ArgumentParser::HANDLER_FUNC},                      \
    { ARG_LONG_NAME, &ArgumentParser::HANDLER_FUNC }

        arg_handler_map_t _handlers = {
            PUNP_ADD_ARG_HANDLER("-V", "--version", version_handler),
            PUNP_ADD_ARG_HANDLER("-h", "--help", help_handler),
            PUNP_ADD_ARG_HANDLER("-v", "--verbose", verbose_handler),
            PUNP_ADD_ARG_HANDLER("-u", "--update", update_handler),
            PUNP_ADD_ARG_HANDLER("-r", "--recursive", recursive_handler),
            PUNP_ADD_ARG_HANDLER("-t", "--threads", threads_handler),
            PUNP_ADD_ARG_HANDLER("-e", "--extension", extension_handler),
            PUNP_ADD_ARG_HANDLER("-E", "--exclude", exclude_handler),
            PUNP_ADD_ARG_HANDLER("-H", "--hidden", hidden_handler),
            PUNP_ADD_ARG_HANDLER("-n", "--dry-run", dry_run_handler),
            PUNP_ADD_ARG_HANDLER("--show-example", "--show-example", show_example_handler), // no short name
        };
#undef PUNP_ADD_ARG_HANDLER

        /*****  Handler methods *****/
        int version_handler(const char *);
        int help_handler(const char *);
        int update_handler(const char *);
        int recursive_handler(const char *);
        int verbose_handler(const char *);
        int threads_handler(const char *);
        int extension_handler(const char *);
        int exclude_handler(const char *);
        int hidden_handler(const char *);
        int dry_run_handler(const char *);
        int show_example_handler(const char *);
        /*****  Handler methods *****/
    };

} // namespace punp
