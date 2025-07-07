#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace PunctuationProcessor {

    class ArgumentParser {
    private:
        std::vector<std::string> _inputs;
        ProcessingConfig _config;
        bool _show_version = false;
        bool _show_help = false;

        int process_args(const std::string &arg, const char *next_arg);

    public:
        ArgumentParser() = default;
        ~ArgumentParser() = default;

        const std::vector<std::string> &inputs() const noexcept { return _inputs; }
        const ProcessingConfig &config() const noexcept { return _config; }
        bool show_version() const noexcept { return _show_version; }
        bool show_help() const noexcept { return _show_help; }

        bool parse(int argc, char *argv[]);
        static void display_version();
        static void display_help(const std::string &name);
    };

} // namespace PunctuationProcessor
