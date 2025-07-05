#include "file_finder.h"
#include "colors.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace PunctuationProcessor {

    std::vector<std::string> FileFinder::find_files(
        const std::vector<std::string> &patterns,
        bool recursive) const {

        std::vector<std::string> all_files;

        for (auto &pattern : patterns) {
            std::vector<std::string> matched_files;

            if (is_dir(pattern)) {
                // If it's a directory, find all files in it
                matched_files = find_files_in_dir(pattern, recursive);
            } else if (pattern.find('*') != std::string::npos ||
                       pattern.find('?') != std::string::npos) {
                // If it contains wildcards, expand glob
                matched_files = expand_glob(pattern);
            } else if (is_file(pattern)) {
                // If it's a regular file, add it directly
                matched_files.emplace_back(pattern);
            } else {
                std::cerr << Colors::YELLOW << "Warning: '" << pattern << "' not found" << '\n'
                          << Colors::RESET;
            }

            // Append to result
            all_files.insert(all_files.end(), matched_files.begin(), matched_files.end());
        }

        // Remove duplicates and sort
        std::sort(all_files.begin(), all_files.end());
        all_files.erase(std::unique(all_files.begin(), all_files.end()), all_files.end());

        return all_files;
    }

    std::vector<std::string> FileFinder::find_files_in_dir(
        const std::string &dir,
        bool recursive) const {

        std::vector<std::string> files;

        try {
            if (recursive) {
                for (auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
                    if (entry.is_regular_file()) {
                        files.emplace_back(entry.path().string());
                    }
                }
            } else {
                files = get_files_from_dir(dir);
            }
        } catch (const std::filesystem::filesystem_error &e) {
            std::cerr << Colors::RED << "Error accessing directory '" << dir << "': "
                      << e.what() << '\n'
                      << Colors::RESET;
        }

        return files;
    }

    std::vector<std::string> FileFinder::expand_glob(const std::string &pattern) const {
        std::vector<std::string> matches;

        // Extract directory and filename pattern
        size_t last_slash = pattern.find_last_of("/\\");
        std::string dir = ".";
        std::string file_pattern = pattern;

        if (last_slash != std::string::npos) {
            dir = pattern.substr(0, last_slash);
            file_pattern = pattern.substr(last_slash + 1);
        }

        try {
            for (const auto &entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (match_glob(filename, file_pattern)) {
                        matches.emplace_back(entry.path().string());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            std::cerr << Colors::RED << "Error expanding glob '" << pattern << "': "
                      << e.what() << '\n'
                      << Colors::RESET;
        }

        return matches;
    }

    bool FileFinder::is_dir(const std::string &path) const {
        try {
            return std::filesystem::is_directory(path);
        } catch (const std::filesystem::filesystem_error &) {
            return false;
        }
    }

    bool FileFinder::is_file(const std::string &path) const {
        try {
            return std::filesystem::is_regular_file(path);
        } catch (const std::filesystem::filesystem_error &) {
            return false;
        }
    }

    bool FileFinder::match_glob(const std::string &filename, const std::string &pattern) const {
        // Simple glob matching for * and ?
        size_t pattern_pos = 0;
        size_t filename_pos = 0;

        while (pattern_pos < pattern.length() && filename_pos < filename.length()) {
            char pat_char = pattern[pattern_pos];

            if (pat_char == '*') {
                // Handle wildcard
                if (pattern_pos + 1 >= pattern.length()) {
                    // * at end matches everything
                    return true;
                }

                // Find next non-wildcard character in pattern
                char next_char = pattern[pattern_pos + 1];

                // Find this character in filename
                while (filename_pos < filename.length() && filename[filename_pos] != next_char) {
                    filename_pos++;
                }

                if (filename_pos >= filename.length()) {
                    return false;
                }

                pattern_pos++;
            } else if (pat_char == '?') {
                // ? matches any single character
                pattern_pos++;
                filename_pos++;
            } else {
                // Regular character match
                if (filename[filename_pos] != pat_char) {
                    return false;
                }
                pattern_pos++;
                filename_pos++;
            }
        }

        // Handle remaining pattern
        while (pattern_pos < pattern.length() && pattern[pattern_pos] == '*') {
            pattern_pos++;
        }

        return pattern_pos >= pattern.length() && filename_pos >= filename.length();
    }

    std::vector<std::string> FileFinder::get_files_from_dir(const std::string &directory) const {
        std::vector<std::string> files;

        try {
            for (const auto &entry : std::filesystem::directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        } catch (const std::filesystem::filesystem_error &e) {
            std::cerr << Colors::RED << "Error reading directory '" << directory << "': "
                      << e.what() << '\n'
                      << Colors::RESET;
        }

        return files;
    }

} // namespace PunctuationProcessor
