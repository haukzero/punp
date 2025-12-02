#pragma once

#include "types.h"
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace punp {

    class ArgumentParser {
    private:
        std::vector<std::string> _inputs;
        ProcessingConfig _config;
        bool _show_version = false;
        bool _show_help = false;
        bool _update = false;

        int process_args(const std::string &arg, const char *next_arg);

    public:
        ArgumentParser() = default;
        ~ArgumentParser() = default;

        const std::vector<std::string> &inputs() const noexcept { return _inputs; }
        const ProcessingConfig &config() const noexcept { return _config; }
        bool show_version() const noexcept { return _show_version; }
        bool show_help() const noexcept { return _show_help; }
        bool update() const noexcept { return _update; }

        bool parse(int argc, char *argv[]);
        static void display_version();
        static void display_help(const std::string &name);

    private:
        using arg_handler_t = int (ArgumentParser::*)(const char *next_arg);
        using arg_handler_map_t = std::unordered_map<std::string, arg_handler_t>;

        arg_handler_map_t _handlers = {
            // version
            {"-V", &ArgumentParser::version_handler},
            {"--version", &ArgumentParser::version_handler},

            // help
            {"-h", &ArgumentParser::help_handler},
            {"--help", &ArgumentParser::help_handler},

            // verbose
            {"-v", &ArgumentParser::verbose_handler},
            {"--verbose", &ArgumentParser::verbose_handler},

            // self update
            {"-u", &ArgumentParser::update_handler},
            {"--update", &ArgumentParser::update_handler},

            // recursive
            {"-r", &ArgumentParser::recursive_handler},
            {"--recursive", &ArgumentParser::recursive_handler},

            // threads
            {"-t", &ArgumentParser::threads_handler},
            {"--threads", &ArgumentParser::threads_handler},

            // file extension
            {"-e", &ArgumentParser::extension_handler},
            {"--extension", &ArgumentParser::extension_handler},
        };

        /*****  Handler methods *****/
        int version_handler(const char *next_arg);
        int help_handler(const char *next_arg);
        int update_handler(const char *next_arg);
        int recursive_handler(const char *next_arg);
        int verbose_handler(const char *next_arg);
        int threads_handler(const char *next_arg);
        int extension_handler(const char *next_arg);
        /*****  Handler methods *****/
    };

} // namespace punp
