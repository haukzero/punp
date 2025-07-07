#include "file_processor.h"
#include "config_manager.h"
#include "thread_pool.h"
#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <fstream>
#include <future>
#include <iostream>
#include <locale>

namespace PunctuationProcessor {

    FileProcessor::FileProcessor(const ConfigManager &config_manager) : _config_manager(config_manager) {}

    std::vector<ProcessingResult> FileProcessor::process_files(
        const std::vector<std::string> &file_paths,
        size_t max_threads) const {

        if (file_paths.empty()) {
            return {};
        }

        // Load all files and create their pages
        std::vector<std::shared_ptr<FileContent>> file_contents;
        std::vector<std::vector<Page>> file_pages;
        file_contents.reserve(file_paths.size());
        file_pages.reserve(file_paths.size());

        for (const auto &file_path : file_paths) {
            auto file_content = load_file_content(file_path);
            if (file_content) {
                file_contents.emplace_back(file_content);
                file_pages.emplace_back(create_pages(file_content));
            } else {
                // Handle file loading error
                file_contents.emplace_back(nullptr);
                file_pages.push_back({});
            }
        }

        // Determine optimal thread count
        size_t n_thread = max_threads;
        size_t hw_max_thread = std::thread::hardware_concurrency();
        if (hw_max_thread > 1) {
            hw_max_thread -= 1; // Reserve one thread for the main thread
        }
        size_t total_pages = 0;
        for (const auto &pages : file_pages) {
            total_pages += pages.size();
        }
        if (n_thread == 0) {
            n_thread = std::min(hw_max_thread, total_pages);
            n_thread = std::max(n_thread, static_cast<size_t>(1));
        } else {
            n_thread = std::min(n_thread, hw_max_thread);
        }
        // Create thread pool
        ThreadPool thread_pool(n_thread);

        // Submit all pages to thread pool
        std::vector<std::vector<std::future<PageResult>>> page_futures;
        page_futures.reserve(file_paths.size());

        for (size_t i = 0; i < file_paths.size(); ++i) {
            const auto &pages = file_pages[i];
            std::vector<std::future<PageResult>> cur_page_future;
            cur_page_future.reserve(pages.size());

            for (const auto &page : pages) {
                cur_page_future.emplace_back(thread_pool.submit([this, page]() {
                    return process_page(page);
                }));
            }

            page_futures.emplace_back(std::move(cur_page_future));
        }

        // Collect results
        std::vector<ProcessingResult> results;
        results.reserve(file_paths.size());

        for (size_t i = 0; i < file_paths.size(); ++i) {
            const auto &file_path = file_paths[i];
            const auto &file_content = file_contents[i];

            if (!file_content) {
                // File loading failed
                ProcessingResult result;
                result.file_path = file_path;
                result.ok = false;
                result.err_msg = "Failed to load file content";
                results.push_back(result);
                continue;
            }

            std::vector<PageResult> page_results;
            page_results.reserve(file_pages[i].size());

            for (auto &page_future : page_futures[i]) {
                try {
                    page_results.emplace_back(page_future.get());
                } catch (const std::exception &e) {
                    PageResult err_page;
                    err_page.file_path = file_path;
                    err_page.ok = false;
                    err_page.err_msg = std::string("Page future exception: ") + e.what();
                    page_results.emplace_back(err_page);
                }
            }

            results.emplace_back(merge_page_results(file_path, page_results));
        }

        return results;
    }

    std::shared_ptr<FileContent> FileProcessor::load_file_content(const std::string &file_path) const {
        try {
            if (!is_text_file(file_path)) {
                return nullptr;
            }

            std::wifstream input_file(file_path);
            if (!input_file) {
                return nullptr;
            }
            input_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

            std::wstring content;
            std::wstring line;
            while (std::getline(input_file, line)) {
                content += line + L"\n";
            }
            input_file.close();

            return std::make_shared<FileContent>(file_path, content);

        } catch (const std::exception &e) {
            return nullptr;
        }
    }

    std::vector<Page> FileProcessor::create_pages(std::shared_ptr<FileContent> fc_ptr) const {
        std::vector<Page> pages;

        if (!fc_ptr || fc_ptr->content.empty()) {
            return pages;
        }

        const auto &content = fc_ptr->content;
        size_t content_size = content.size();

        if (content_size <= _page_config.page_size) {
            // Single page for small files
            Page sp(fc_ptr, 0, 0, content_size);
            pages.emplace_back(std::move(sp));
            return pages;
        }

        // Multiple pages for large files
        size_t page_id = 0;
        size_t start_pos = 0;

        while (start_pos < content_size) {
            size_t end_pos = std::min(start_pos + _page_config.page_size, content_size);

            // Try to find a good boundary (line break or space)
            if (end_pos < content_size) {
                size_t search_start = std::max(start_pos, end_pos - 100);
                size_t line_break_pos = content.rfind(L'\n', end_pos);

                if (line_break_pos != std::wstring::npos && line_break_pos > search_start) {
                    end_pos = line_break_pos + 1;
                } else {
                    size_t space_pos = content.rfind(L' ', end_pos);
                    if (space_pos != std::wstring::npos && space_pos > search_start) {
                        end_pos = space_pos + 1;
                    }
                }
            }

            pages.emplace_back(fc_ptr, page_id++, start_pos, end_pos);
            start_pos = end_pos;
        }

        return pages;
    }

    PageResult FileProcessor::process_page(const Page &page) const {
        PageResult result;
        result.file_path = page.f_ptr->filename;
        result.page_id = page.pid;
        result.ok = true;
        result.n_rep = 0;

        try {
            // Extract page content
            const auto &full_content = page.f_ptr->content;
            result.processed_content = full_content.substr(page.start_pos, page.end_pos - page.start_pos);

            // Apply replacements
            result.n_rep = apply_replace(result.processed_content);

        } catch (const std::exception &e) {
            result.ok = false;
            result.err_msg = std::string("Page processing exception: ") + e.what();
        }

        return result;
    }

    ProcessingResult FileProcessor::merge_page_results(
        const std::string &file_path,
        const std::vector<PageResult> &page_results) const {

        ProcessingResult result;
        result.file_path = file_path;
        result.ok = false;
        result.n_rep = 0;

        try {
            // Check if any page failed
            for (const auto &page_result : page_results) {
                if (!page_result.ok) {
                    result.err_msg = page_result.err_msg;
                    return result;
                }
            }

            // Sort page results by page_id to ensure correct order
            auto sorted_results = page_results;
            std::sort(sorted_results.begin(), sorted_results.end(),
                      [](const PageResult &a, const PageResult &b) {
                          return a.page_id < b.page_id;
                      });

            // Reconstruct content and count replacements
            std::wstring processed_content;
            size_t total_size = 0;
            for (const auto &page_result : sorted_results) {
                total_size += page_result.processed_content.size();
                result.n_rep += page_result.n_rep;
            }

            processed_content.reserve(total_size);
            for (const auto &page_result : sorted_results) {
                processed_content += page_result.processed_content;
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
            result.err_msg = std::string("Page merging exception: ") + e.what();
        }

        return result;
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

} // namespace PunctuationProcessor
