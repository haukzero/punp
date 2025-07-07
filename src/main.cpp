#include "argument_parser.h"
#include "common.h"
#include "config_manager.h"
#include "file_finder.h"
#include "file_processor.h"
#include <chrono>
#include <iostream>

using namespace PunctuationProcessor;

int main(int argc, char *argv[]) {
    auto start = std::chrono::high_resolution_clock::now();

    // Parse command line arguments
    ArgumentParser parser;
    if (!parser.parse(argc, argv)) {
        std::cerr << Colors::RED << "Error: No input files specified" << '\n'
                  << Colors::RESET;
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

    auto &config = parser.config();

    // Load configuration
    ConfigManager config_manager;
    if (!config_manager.load(config.verbose)) {
        std::cerr << Colors::RED << "Error: Failed to load configuration" << '\n'
                  << Colors::RESET;
        return 1;
    }

    if (config_manager.empty()) {
        std::cerr << Colors::RED << "Error: No replacement rules found in configuration" << '\n'
                  << Colors::RESET;
        return 1;
    }

    // Find files to process
    FileFinder file_finder;
    auto file_paths = file_finder.find_files(parser.inputs(), config.recursive);

    if (file_paths.empty()) {
        std::cerr << Colors::RED << "Error: No files found to process" << '\n'
                  << Colors::RESET;
        return 1;
    }

    if (config.verbose) {
        std::cout << Colors::BLUE << "Found " << file_paths.size() << " files to process" << '\n'
                  << Colors::RESET;
    }

    // Process files
    FileProcessor processor(config_manager);
    auto results = processor.process_files(file_paths, config.max_threads);

    // Report results
    size_t n_ok = 0;
    size_t n_rep_total = 0;

    for (const auto &result : results) {
        if (result.ok) {
            n_ok++;
            n_rep_total += result.n_rep;

            if (config.verbose) {
                std::cout << "- Processed: " << result.file_path;
                if (result.n_rep > 0) {
                    std::cout << " (" << result.n_rep << " replacements)";
                }
                std::cout << '\n';
            }
        } else {
            std::cerr << Colors::RED << "Failed to process " << result.file_path
                      << ": " << result.err_msg << '\n'
                      << Colors::RESET;
        }
    }

    // Summary
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << Colors::GREEN << "\nProcessing complete:\n";
    std::cout << Colors::BLUE << "  Files processed: " << n_ok << "/" << results.size() << '\n';
    std::cout << "  Total replacements: " << n_rep_total << '\n';
    std::cout << "  Time taken: " << duration.count() << " ms" << '\n';
    std::cout << Colors::RESET;

    return (n_ok == results.size()) ? 0 : 1;
}
