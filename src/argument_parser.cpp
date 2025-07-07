#include "argument_parser.h"
#include "common.h"
#include <iostream>

namespace PunctuationProcessor {

    bool ArgumentParser::parse(int argc, char *argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            const char *next_arg = (i + 1 < argc) ? argv[i + 1] : nullptr;

            int consumed = process_args(arg, next_arg);
            i += consumed - 1; // Adjust index for consumed arguments
        }

        return !_inputs.empty() || _show_help || _show_version;
    }

    int ArgumentParser::process_args(const std::string &arg, const char *next_arg) {
        if (arg == "-V" || arg == "--version") {
            _show_version = true;
            return 1;
        }

        if (arg == "-h" || arg == "--help") {
            _show_help = true;
            return 1;
        }

        if (arg == "-r" || arg == "--recursive") {
            _config.recursive = true;
            return 1;
        }

        if (arg == "-v" || arg == "--verbose") {
            _config.verbose = true;
            return 1;
        }

        if (arg == "-t" || arg == "--threads") {
            if (next_arg) {
                try {
                    _config.max_threads = std::stoul(next_arg);
                    return 2;
                } catch (const std::exception &) {
                    std::cerr << "Warning: Invalid thread count '" << next_arg
                              << "', using auto-detection" << std::endl;
                    return 2;
                }
            } else {
                std::cerr << "Warning: --threads requires a number" << std::endl;
                return 1;
            }
        }

        // If it starts with -, it's an unknown option
        if (arg.front() == '-') {
            std::cerr << "Warning: Unknown option '" << arg << "'" << std::endl;
            return 1;
        }

        // Otherwise, it's an input file or pattern
        _inputs.emplace_back(arg);
        return 1;
    }

    void ArgumentParser::display_version() {
        std::cout << "v" << Version::VERSION << '\n';
    }

    void ArgumentParser::display_help(const std::string &programName) {
        std::cout << Colors::GREEN << "Usage: " << programName << " [OPTIONS] <files...>\n";
        std::cout << Colors::CYAN << "High-performance punctuation replacement tool\n";
        std::cout << Colors::GREEN << "Options:\n";
        std::cout << Colors::BLUE << "  -V, --version" << Colors::YELLOW << "           Show version information\n";
        std::cout << Colors::BLUE << "  -h, --help" << Colors::YELLOW << "              Show this help message\n";
        std::cout << Colors::BLUE << "  -r, --recursive" << Colors::YELLOW << "         Process directories recursively\n";
        std::cout << Colors::BLUE << "  -v, --verbose" << Colors::YELLOW << "           Enable verbose output\n";
        std::cout << Colors::BLUE << "  -t, --threads <n>" << Colors::YELLOW << "       Set maximum thread count (default: auto)\n";
        std::cout << Colors::GREEN << "Examples:\n";
        std::cout << Colors::MAGENTA << "  " << programName << " file.txt" << Colors::YELLOW << "               # Process single file\n";
        std::cout << Colors::MAGENTA << "  " << programName << " *.txt" << Colors::YELLOW << "                  # Process all .txt files\n";
        std::cout << Colors::MAGENTA << "  " << programName << " -r ./docs" << Colors::YELLOW << "              # Process all files in docs/ recursively\n";
        std::cout << Colors::MAGENTA << "  " << programName << " -v -t 4 *.md" << Colors::YELLOW << "           # Process .md files with 4 threads, verbose\n\n";
        std::cout << Colors::GREEN << "Configuration:\n";
        std::cout << Colors::CYAN << "  The tool looks for '.prules' in:\n";
        std::cout << Colors::CYAN << "    1. Current directory (higher priority)\n";
        std::cout << Colors::CYAN << "    2. ~/.local/share/punp/ (lower priority)\n";
        std::cout << Colors::CYAN << "  Rules in higher priority locations override those in lower priority locations.\n"
                  << Colors::RESET;
    }

} // namespace PunctuationProcessor
