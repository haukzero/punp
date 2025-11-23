#include "argument_parser.h"
#include "color_print.h"
#include "common.h"

namespace punp {

    bool ArgumentParser::parse(int argc, char *argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            const char *next_arg = (i + 1 < argc) ? argv[i + 1] : nullptr;

            int consumed = process_args(arg, next_arg);
            i += consumed - 1; // Adjust index for consumed arguments
        }

        return !_inputs.empty() || _show_help || _show_version || _update;
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

        if (arg == "-u" || arg == "--update") {
            _update = true;
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
                    warn("Invalid thread count '", next_arg, "', using auto-detection");
                    return 2;
                }
            } else {
                error("--threads requires a number");
                return 1;
            }
        }

        // If it starts with -, it's an unknown option
        if (arg.front() == '-') {
            error("Unknown option '", arg, "'");
            return 1;
        }

        // Otherwise, it's an input file or pattern
        _inputs.emplace_back(arg);
        return 1;
    }

    void ArgumentParser::display_version() {
        println("v", Version::VERSION);
    }

    void ArgumentParser::display_help(const std::string &programName) {

        using kv_pair_t = std::pair<std::string, std::string>;
        using kv_vector_t = std::vector<kv_pair_t>;

        auto print_aligned_kv_pairs = [](const kv_vector_t &vec) {
            size_t max_align_width = 0;
            for (const auto &kv : vec) {
                if (kv.first.size() > max_align_width) {
                    max_align_width = kv.first.size();
                }
            }
            size_t col_width = max_align_width + 4;

            for (const auto &kv : vec) {
                print_blue("  ", kv.first);

                size_t printed_len = 2 + kv.first.size();
                size_t pad = (col_width > printed_len) ? (col_width - printed_len) : 1;
                for (size_t i = 0; i < pad; ++i) {
                    print(" ");
                }

                println_yellow(kv.second);
            }
        };

        println_green("Usage: ", programName, " [OPTIONS] <files...>");
        println_cyan("High-performance punctuation replacement tool");

        println_green("Options:");
        kv_vector_t options = {
            {"-V, --version", "Show version information"},
            {"-h, --help", "Show this help message"},
            {"-u, --update", "Update the tool to the latest version"},
            {"-r, --recursive", "Process directories recursively"},
            {"-v, --verbose", "Enable verbose output"},
            {"-t, --threads <n>", "Set maximum thread count (default: auto)"}};
        print_aligned_kv_pairs(options);

        println_green("Examples:");
        kv_vector_t examples = {
            {programName + " file.txt", "Process single file"},
            {programName + " *.txt", "Process all .txt files"},
            {programName + " -r ./docs", "Process all files in docs/ recursively"},
            {programName + " -v -t 4 *.md", "Process .md files with 4 threads, verbose"},
        };
        print_aligned_kv_pairs(examples);

        println_green("Configuration:");
        println_cyan("  The tool looks for '", RuleFile::NAME, "' in:");
        println_cyan("    1. Current directory (higher priority)");
        println_cyan("    2. ", StoreDir::CONFIG_DIR, " (lower priority)");
        println_cyan("  Rules in higher priority locations override those in lower priority locations.");
    }

} // namespace punp
