#include "file_processor.h"
#include "config_manager.h"
#include "thread_pool.h"
#include <algorithm>
#include <codecvt>
#include <cstddef>
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

        return process_small_file(file_path);
    }

    std::vector<ProcessingResult> FileProcessor::process_files(
        const std::vector<std::string> &file_paths,
        size_t max_threads) const {

        if (file_paths.empty()) {
            return {};
        }

        // Determine optimal thread count
        size_t n_thread = max_threads;
        size_t hw_max_thread = std::thread::hardware_concurrency();
        if (n_thread == 0) {
            n_thread = std::min(file_paths.size(), hw_max_thread - 1); // one thread for main task
            n_thread = std::max(n_thread, static_cast<size_t>(1));
        } else if (n_thread > hw_max_thread) {
            // should not create threads more than hw_max_thread
            n_thread = hw_max_thread - 1;
        }

        // Single-threaded processing for small workloads
        if (file_paths.size() == 1 || n_thread == 1) {
            std::vector<ProcessingResult> results;
            results.reserve(file_paths.size());
            for (auto &file_path : file_paths) {
                results.emplace_back(process_file(file_path));
            }
            return results;
        }

        // Multi-threaded processing
        ThreadPool thread_pool(n_thread);

        // Analyze all files to determine processing strategy
        std::vector<FileAnalysis> file_analyses;
        file_analyses.reserve(file_paths.size());

        for (auto &file_path : file_paths) {
            file_analyses.emplace_back(analyze_file(file_path, n_thread));
        }

        // Create task queues
        std::vector<std::future<ProcessingResult>> file_futures;
        std::vector<std::vector<std::future<ChunkResult>>> chunk_futures;

        // Submit tasks based on analysis
        for (size_t i = 0; i < file_analyses.size(); ++i) {
            const auto &analysis = file_analyses[i];

            if (!analysis.is_large_file || analysis.chunk_boundaries.size() < 3) {
                // Process as single file
                file_futures.emplace_back(thread_pool.submit([this, analysis]() {
                    return process_small_file(analysis.file_path);
                }));
                chunk_futures.emplace_back(); // Empty vector for this file
            } else {
                // Process as chunks
                file_futures.emplace_back(); // No direct file future for chunked files

                std::vector<std::future<ChunkResult>> current_chunk_futures;
                size_t num_chunks = analysis.chunk_boundaries.size() - 1;
                current_chunk_futures.reserve(num_chunks);

                for (size_t j = 0; j < num_chunks; ++j) {
                    size_t start = analysis.chunk_boundaries[j];
                    size_t end = analysis.chunk_boundaries[j + 1];
                    std::wstring chunk = analysis.content.substr(start, end - start);

                    current_chunk_futures.emplace_back(thread_pool.submit([this, chunk]() {
                        return process_chunk(chunk);
                    }));
                }

                chunk_futures.emplace_back(std::move(current_chunk_futures));
            }
        }

        // Collect results
        std::vector<ProcessingResult> results;
        results.reserve(file_paths.size());

        for (size_t i = 0; i < file_analyses.size(); ++i) {
            const auto &analysis = file_analyses[i];

            if (!analysis.is_large_file || analysis.chunk_boundaries.size() < 3) {
                // Get result from single file processing
                try {
                    results.emplace_back(file_futures[i].get());
                } catch (const std::exception &e) {
                    ProcessingResult err_res;
                    err_res.file_path = analysis.file_path;
                    err_res.ok = false;
                    err_res.err_msg = std::string("Future exception: ") + e.what();
                    results.emplace_back(err_res);
                }
            } else {
                // Collect chunk results and merge
                std::vector<ChunkResult> chunk_results;
                chunk_results.reserve(chunk_futures[i].size());

                for (auto &chunk_future : chunk_futures[i]) {
                    try {
                        chunk_results.emplace_back(chunk_future.get());
                    } catch (const std::exception &e) {
                        ChunkResult err_chunk;
                        err_chunk.ok = false;
                        err_chunk.err_msg = std::string("Chunk future exception: ") + e.what();
                        chunk_results.emplace_back(err_chunk);
                    }
                }

                results.emplace_back(merge_chunk_results(analysis.file_path, chunk_results));
            }
        }

        return results;
    }

    size_t FileProcessor::apply_replace(std::wstring &text) const {
        auto &rep_map = _config_manager.replacement_map();
        if (rep_map.empty()) {
            return 0;
        }

        size_t n_rep_total = 0;

        for (const auto &[from, to] : rep_map) {
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

    FileProcessor::FileAnalysis FileProcessor::analyze_file(
        const std::string &file_path,
        const size_t available_threads) const {

        FileAnalysis analysis;
        analysis.file_path = file_path;
        analysis.is_large_file = false;

        try {
            // Read file
            std::wifstream input_file(file_path);
            if (!input_file) {
                return analysis; // Return empty analysis on read error
            }
            input_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

            // Read all content
            std::wstring line;
            while (std::getline(input_file, line)) {
                analysis.content += line + L"\n";
            }
            input_file.close();

            // Determine if chunking should be used
            analysis.is_large_file = should_use_chunking(analysis.content, available_threads);

            if (analysis.is_large_file) {
                analysis.chunk_boundaries = cal_chunk_boundaries(analysis.content, _chunk_config.chunk_size);
            }

        } catch (const std::exception &e) {
            // Return empty analysis on error
            analysis.content.clear();
            analysis.is_large_file = false;
        }

        return analysis;
    }

    bool FileProcessor::should_use_chunking(
        const std::wstring &content,
        const size_t available_threads) const {

        // Check if file is large enough and we have enough threads to justify chunking
        size_t content_size = content.size() * sizeof(wchar_t);
        bool is_large_enough = content_size >= _chunk_config.min_file_size_for_chunking;

        if (!is_large_enough) {
            return false;
        }

        // Calculate potential number of chunks
        size_t potential_chunks = content_size / _chunk_config.chunk_size;
        if (potential_chunks < _chunk_config.min_chunks_per_file) {
            return false;
        }

        // Check if we have enough threads to justify chunking
        return potential_chunks >= 2 && available_threads >= 2;
    }

    ProcessingResult FileProcessor::process_small_file(const std::string &file_path) const {
        ProcessingResult result;
        result.file_path = file_path;
        result.ok = false;
        result.n_rep = 0;

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

    ProcessingResult FileProcessor::merge_chunk_results(
        const std::string &file_path,
        const std::vector<ChunkResult> &chunk_results) const {

        ProcessingResult result;
        result.file_path = file_path;
        result.ok = false;
        result.n_rep = 0;

        try {
            // Check if any chunk failed
            for (const auto &chunk_result : chunk_results) {
                if (!chunk_result.ok) {
                    result.err_msg = chunk_result.err_msg;
                    return result;
                }
            }

            // Reconstruct content and count replacements
            std::wstring processed_content;
            size_t total_size = 0;
            for (const auto &chunk_result : chunk_results) {
                total_size += chunk_result.content.size();
                result.n_rep += chunk_result.n_rep;
            }

            processed_content.reserve(total_size);
            for (const auto &chunk_result : chunk_results) {
                processed_content += chunk_result.content;
            }

            // Write back to file if changes were made
            if (result.n_rep > 0) {
                std::wofstream output_file(file_path);
                if (!output_file) {
                    result.err_msg = "Cannot open file for writing";
                    return result;
                }
                output_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
                output_file << processed_content;
                output_file.close();
            }

            result.ok = true;

        } catch (const std::exception &e) {
            result.err_msg = std::string("Chunk merging exception: ") + e.what();
        }

        return result;
    }

    std::vector<size_t> FileProcessor::cal_chunk_boundaries(
        const std::wstring &content,
        const size_t chunk_size) const {

        std::vector<size_t> boundaries;
        boundaries.emplace_back(0);

        size_t pos = 0;
        while (pos < content.size()) {
            size_t next_pos = std::min(pos + chunk_size, content.size());

            // If we're not at the end, try to find a good boundary
            if (next_pos < content.size()) {
                // Look for line break within reasonable distance
                size_t search_start = std::max(pos, next_pos - 100);
                size_t line_break_pos = content.rfind(L'\n', next_pos);

                if (line_break_pos != std::wstring::npos && line_break_pos > search_start) {
                    next_pos = line_break_pos + 1;
                } else {
                    // Look for space/whitespace as fallback
                    size_t space_pos = content.rfind(L' ', next_pos);
                    if (space_pos != std::wstring::npos && space_pos > search_start) {
                        next_pos = space_pos + 1;
                    }
                }
            }

            pos = next_pos;
            if (pos < content.size()) {
                boundaries.emplace_back(pos);
            }
        }

        boundaries.push_back(content.size());
        return boundaries;
    }

    ChunkResult FileProcessor::process_chunk(const std::wstring &chunk_content) const {
        ChunkResult result;
        result.content = chunk_content;
        result.ok = true;
        result.n_rep = 0;

        try {
            result.n_rep = apply_replace(result.content);
        } catch (const std::exception &e) {
            result.ok = false;
            result.err_msg = std::string("Chunk processing exception: ") + e.what();
        }

        return result;
    }

} // namespace PunctuationProcessor
