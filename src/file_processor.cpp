#include "file_processor.h"
#include "config_manager.h"
#include "thread_pool.h"
#include <algorithm>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <locale>

namespace PunctuationProcessor {

    FileProcessor::FileProcessor(const ConfigManager &config_manager) : _config_manager(config_manager) {}

    ProcessingResult FileProcessor::process_file(const std::string &file_path) const {
        ProcessingResult result;
        result.file_path = file_path;
        result.ok = false;
        result.n_rep = 0;

        // Check if file exists and is readable
        if (!std::filesystem::exists(file_path)) {
            result.err_msg = "File does not exist";
            return result;
        }

        if (!is_text_file(file_path)) {
            result.err_msg = "File appears to be binary";
            return result;
        }

        try {
            // Read file
            std::wifstream input_file(file_path);
            if (!input_file) {
                result.err_msg = "Cannot open file for reading";
                return result;
            }
            input_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

            // Read all content
            std::wstring content;
            std::wstring line;
            while (std::getline(input_file, line)) {
                content += line + L"\n";
            }
            input_file.close();

            // Apply replacements
            result.n_rep = apply_replace(content);

            // Write back to file if changes were made
            if (result.n_rep > 0) {
                std::wofstream output_file(file_path);
                if (!output_file) {
                    result.err_msg = "Cannot open file for writing";
                    return result;
                }
                output_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
                output_file << content;
                output_file.close();
            }

            result.ok = true;
        } catch (const std::exception &e) {
            result.err_msg = std::string("Exception: ") + e.what();
        }

        return result;
    }

    std::vector<ProcessingResult> FileProcessor::process_files(
        const std::vector<std::string> &file_paths,
        size_t max_threads) const {

        if (file_paths.empty()) {
            return {};
        }

        // Determine optimal thread count
        size_t n_thread = max_threads;
        if (n_thread == 0) {
            n_thread = std::min(file_paths.size(),
                                static_cast<size_t>(std::thread::hardware_concurrency()));
            n_thread = std::max(n_thread, static_cast<size_t>(1));
        }

        // Single-threaded processing for small workloads
        if (file_paths.size() == 1 || n_thread == 1) {
            std::vector<ProcessingResult> results;
            results.reserve(file_paths.size());
            for (const auto &filePath : file_paths) {
                results.emplace_back(process_file(filePath));
            }
            return results;
        }

        // Multi-threaded processing
        ThreadPool thread_pool(n_thread);
        std::vector<std::future<ProcessingResult>> futures;
        futures.reserve(file_paths.size());

        // Submit all tasks
        for (auto &file_path : file_paths) {
            futures.emplace_back(thread_pool.submit([this, file_path]() {
                return process_file(file_path);
            }));
        }

        // Collect results
        std::vector<ProcessingResult> results;
        results.reserve(file_paths.size());
        for (auto &future : futures) {
            try {
                results.emplace_back(future.get());
            } catch (const std::exception &e) {
                ProcessingResult err_res;
                err_res.file_path = "unknown";
                err_res.ok = false;
                err_res.err_msg = std::string("Future exception: ") + e.what();
                results.emplace_back(err_res);
            }
        }

        return results;
    }

    size_t FileProcessor::apply_replace(std::wstring &text) const {
        auto &replacementMap = _config_manager.replacement_map();
        if (replacementMap.empty()) {
            return 0;
        }

        size_t n_rep_total = 0;

        for (const auto &[from, to] : replacementMap) {
            if (from.empty())
                continue;

            size_t pos = 0;
            size_t n_rep = 0;

            while ((pos = text.find(from, pos)) != std::wstring::npos) {
                text.replace(pos, from.length(), to);
                pos += to.length();
                n_rep++;
            }

            n_rep_total += n_rep;
        }

        return n_rep_total;
    }

    bool FileProcessor::is_text_file(const std::string &filePath) const {
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            return false;
        }

        // Read first 1KB to check for binary content
        constexpr size_t sample_size = 1024;
        std::vector<char> buffer(sample_size);
        file.read(buffer.data(), sample_size);

        size_t bytes_read = static_cast<size_t>(file.gcount());
        file.close();

        // Check for null bytes (common in binary files)
        size_t null_bytes = std::count(buffer.begin(), buffer.begin() + bytes_read, '\0');

        // If more than 1% null bytes, likely binary
        return (null_bytes * 100 / std::max(bytes_read, size_t(1))) < 1;
    }

} // namespace PunctuationProcessor
