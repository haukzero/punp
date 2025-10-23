#include "file_processor.h"
#include "common.h"
#include "config_manager.h"
#include "thread_pool.h"
#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <fstream>
#include <future>
#include <iostream>
#include <locale>

namespace punp {

    FileProcessor::FileProcessor(const ConfigManager &config_manager)
        : _thread_pool(ThreadPool(1)),
          _writeback_stop(false) {

        // Initialize the AC automaton with the replacement map
        _ac_automaton.build_from_map(config_manager.replacement_map());
        // Set protected regions
        _ac_automaton.set_protected_regions(config_manager.protected_regions());
        // Start the writeback thread
        _writeback_thread = std::thread(&FileProcessor::writeback_worker, this);
    }

    FileProcessor::~FileProcessor() {
        // shutdown writeback thread
        {
            std::lock_guard<std::mutex> lock(_writeback_mtx);
            _writeback_stop = true;
        }
        _writeback_cv.notify_all();

        if (_writeback_thread.joinable()) {
            _writeback_thread.join();
        }
        // Shutdown the thread pool
        _thread_pool.shutdown();
    }

    std::vector<ProcessingResult> FileProcessor::process_files(
        const std::vector<std::string> &file_paths,
        size_t max_threads) {

        if (file_paths.empty()) {
            return {};
        }

        size_t hw_max_thread = std::thread::hardware_concurrency();
        if (hw_max_thread > 2) {
            hw_max_thread -= 2; // Reserve tow thread for the main thread and writeback thread
        }

        std::vector<std::shared_ptr<FileContent>> file_contents(file_paths.size());
        std::vector<std::vector<Page>> file_pages(file_paths.size());
        size_t loading_threads = max_threads;
        if (max_threads == 0) {
            loading_threads = std::min(file_paths.size(), hw_max_thread);
            loading_threads = std::max(loading_threads, static_cast<size_t>(1));
        } else {
            loading_threads = std::min(loading_threads, hw_max_thread);
        }

        // Scaling the thread pool for loading files
        _thread_pool.scaling(loading_threads);

        // Submit file loading tasks
        std::vector<std::future<std::pair<std::shared_ptr<FileContent>, std::vector<Page>>>> loading_futures;
        loading_futures.reserve(file_paths.size());

        for (size_t i = 0; i < file_paths.size(); ++i) {
            loading_futures.emplace_back(
                _thread_pool.submit([this, &file_paths, i]() -> std::pair<std::shared_ptr<FileContent>, std::vector<Page>> {
                    auto file_content = load_file_content(file_paths[i]);
                    if (file_content) {
                        auto pages = create_pages(file_content);
                        return std::make_pair(file_content, std::move(pages));
                    } else {
                        return std::make_pair(nullptr, std::vector<Page>{});
                    }
                }));
        }

        // Collect loading results
        for (size_t i = 0; i < file_paths.size(); ++i) {
            try {
                auto result = loading_futures[i].get();
                file_contents[i] = result.first;
                file_pages[i] = std::move(result.second);
            } catch (const std::exception &e) {
                // Handle loading error
                file_contents[i] = nullptr;
                file_pages[i] = {};
            }
        }

        // Determine optimal thread count
        size_t n_thread = max_threads;
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
        // Scaling the thread pool to the optimal number of threads
        _thread_pool.scaling(n_thread);

        // Submit all pages to thread pool
        std::vector<std::vector<std::future<PageResult>>> page_futures;
        page_futures.reserve(file_paths.size());

        for (size_t i = 0; i < file_paths.size(); ++i) {
            const auto &pages = file_pages[i];
            std::vector<std::future<PageResult>> cur_page_future;
            cur_page_future.reserve(pages.size());

            for (const auto &page : pages) {
                cur_page_future.emplace_back(_thread_pool.submit([this, page]() {
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

            ProcessingResult result;
            result.file_path = file_path;
            result.ok = true;
            result.n_rep = 0;

            if (!file_content) {
                // File loading failed
                result.ok = false;
                result.err_msg = "Failed to load file content";
                results.emplace_back(result);
                continue;
            }

            for (auto &page_future : page_futures[i]) {
                try {
                    auto page_result = page_future.get();
                    if (!page_result.ok) {
                        result.ok = false;
                        result.err_msg = page_result.err_msg;
                        break;
                    }
                    result.n_rep += page_result.n_rep;
                } catch (const std::exception &e) {
                    result.ok = false;
                    result.err_msg = std::string("Page future exception: ") + e.what();
                    break;
                }
            }

            results.emplace_back(result);
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

        if (content_size <= PageConfig::SIZE) {
            // Single page for small files
            Page sp(fc_ptr, 0, 0, content_size);
            pages.emplace_back(std::move(sp));
            fc_ptr->ref_cnt = 1;
            fc_ptr->processed_pages.resize(1);

            return pages;
        }

        // Multiple pages for large files
        size_t page_id = 0;
        size_t start_pos = 0;

        while (start_pos < content_size) {
            size_t end_pos = std::min(start_pos + PageConfig::SIZE, content_size);

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

        fc_ptr->ref_cnt = static_cast<int>(pages.size());
        fc_ptr->processed_pages.resize(pages.size());

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

            page.f_ptr->total_replacements.fetch_add(result.n_rep);

            {
                std::lock_guard<std::mutex> lock(page.f_ptr->processed_mutex);
                if (page.f_ptr->processed_pages.size() <= page.pid) {
                    page.f_ptr->processed_pages.resize(page.pid + 1);
                }
                page.f_ptr->processed_pages[page.pid] = result.processed_content;
            }

            int remaining = page.f_ptr->ref_cnt.fetch_sub(1) - 1;
            if (remaining == 0) {
                size_t total_reps = page.f_ptr->total_replacements.load();
                const_cast<FileProcessor *>(this)->notify_writeback(page.f_ptr, total_reps);
            }

        } catch (const std::exception &e) {
            result.ok = false;
            result.err_msg = std::string("Page processing exception: ") + e.what();
        }

        return result;
    }

    void FileProcessor::writeback_worker() {
        while (true) {
            std::unique_lock<std::mutex> lock(_writeback_mtx);

            _writeback_cv.wait(lock, [this] {
                return !_writeback_queue.empty() || _writeback_stop;
            });

            if (_writeback_stop && _writeback_queue.empty()) {
                break;
            }

            // If thread pool is available and has idle threads, try to batch process
            if (_thread_pool.has_idle_threads() && !_writeback_queue.empty()) {
                std::vector<WritebackNotification> batch;

                // Extract up to idle_count items from queue
                while (!_writeback_queue.empty() && batch.size() < _thread_pool.idle_threads()) {
                    batch.emplace_back(_writeback_queue.front());
                    _writeback_queue.pop();
                }

                lock.unlock();

                // Submit batch to thread pool
                for (const auto &notification : batch) {
                    _thread_pool.submit([this, notification]() {
                        writeback(notification.file_content, notification.total_replacements);
                    });
                }
            } else if (!_writeback_queue.empty()) {
                // Process single item locally
                auto notification = _writeback_queue.front();
                _writeback_queue.pop();
                lock.unlock();

                writeback(notification.file_content, notification.total_replacements);
            }
        }
    }

    void FileProcessor::notify_writeback(std::shared_ptr<FileContent> file_content, size_t replacements) {
        std::lock_guard<std::mutex> lock(_writeback_mtx);
        _writeback_queue.emplace(file_content, replacements);
        _writeback_cv.notify_one();
    }

    bool FileProcessor::writeback(const std::shared_ptr<FileContent> &file_content, size_t total_replacements) const {
        try {
            if (total_replacements == 0) {
                return true;
            }

            std::wstring complete_content;
            {
                std::lock_guard<std::mutex> lock(file_content->processed_mutex);

                size_t total_size = 0;
                for (const auto &page_content : file_content->processed_pages) {
                    total_size += page_content.size();
                }
                complete_content.reserve(total_size);

                for (const auto &page_content : file_content->processed_pages) {
                    complete_content += page_content;
                }
            }

            std::wofstream output_file(file_content->filename);
            if (!output_file) {
                std::cerr << "Cannot open file for writing: " << file_content->filename << std::endl;
                return false;
            }

            output_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));
            output_file << complete_content;
            output_file.close();

            return true;

        } catch (const std::exception &e) {
            std::cerr << "Error writing file " << file_content->filename << ": " << e.what() << std::endl;
            return false;
        }
    }

    size_t FileProcessor::apply_replace(std::wstring &text) const {
        return _ac_automaton.apply_replace(text);
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

} // namespace punp
