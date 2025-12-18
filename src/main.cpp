#include "base/color_print.h"
#include "config/argument_parser.h"
#include "config/config_manager.h"
#include "core/file_finder.h"
#include "core/file_processor.h"
#include "updater/updater.h"

#include <chrono>

using namespace punp;

int main(int argc, char *argv[]) {
    auto start = std::chrono::high_resolution_clock::now();

    // Parse command line arguments
    ArgumentParser parser;
    if (!parser.parse(argc, argv)) {
        error("No input files specified");
        ArgumentParser::display_help(argv[0]);
        return 1;
    }

    if (parser.show_version()) {
        ArgumentParser::display_version();
        return 0;
    }

    if (parser.show_help()) {
        ArgumentParser::display_help(argv[0]);
        return 0;
    }

    if (parser.show_example()) {
        ArgumentParser::display_example(argv[0]);
        return 0;
    }

    if (parser.update()) {
        Updater updater;
        updater.maybe_update(parser.update_type());
        return 0;
    }

    auto &config = parser.config();

    if (!config.finder_config.extensions.empty() && config.finder_config.patterns.empty()) {
        error("When using `-e`/`--extension`, you must specify files or directories to process");
        return 1;
    }

    // Load configuration
    ConfigManager config_manager;
    if (!config_manager.load(parser.verbose())) {
        error("Failed to load configuration");
        return 1;
    }

    if (config_manager.empty()) {
        error("No replacement rules found in configuration");
        return 1;
    }

    // Find files to process
    FileFinder file_finder;
    auto file_paths = file_finder.find_files(config.finder_config);

    if (file_paths.empty()) {
        error("No files found to process");
        return 1;
    }

    if (parser.verbose() || parser.dry_run()) {
        println_blue("Found ", file_paths.size(), " files to process");
    }

    if (parser.dry_run()) {
        println_yellow("These files will be processed (dry run, no changes will be made):");
        for (const auto &file : file_paths) {
            println("  ", file);
        }
        return 0;
    }

    // Process files
    config.processor_config.file_paths = file_paths;
    FileProcessor processor(config_manager);
    auto results = processor.process_files(config.processor_config);

    // Report results
    size_t n_ok = 0;
    size_t n_rep_total = 0;

    for (const auto &result : results) {
        if (result.ok) {
            n_ok++;
            n_rep_total += result.n_rep;

            if (parser.verbose()) {
                println_blue("- Processed: ", result.file_path);
                if (result.n_rep > 0) {
                    println_blue(" (", result.n_rep, " replacements)");
                }
            }
        } else {
            error("Failed to process ", result.file_path, ": ", result.err_msg);
        }
    }

    // Summary
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    println_green("Processing complete:");
    println_blue("  Files processed: ", n_ok, "/", results.size());
    println_blue("  Total replacements: ", n_rep_total);
    println_blue("  Time taken: ", duration.count(), " ms");

    return (n_ok == results.size()) ? 0 : 1;
}
