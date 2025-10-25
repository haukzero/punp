#include "file_processor.h"
#include "common.h"
#include "config_manager.h"
#include "thread_pool.h"
#include "types.h"
#include <algorithm>
#include <codecvt>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

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
        size_t num_files = file_paths.size();

        std::vector<std::shared_ptr<FileContent>> file_contents(num_files);
        std::vector<std::vector<Page>> file_pages(num_files);
        std::vector<std::vector<PageResult>> page_results(num_files);

        size_t num_threads = max_threads;
        if (max_threads == 0) {
            num_threads = std::min(num_files * 2, Hardware::AUTO_NUM_THREADS);
            num_threads = std::max(num_threads, static_cast<size_t>(1));
        } else {
            num_threads = std::min(num_threads, Hardware::AUTO_NUM_THREADS);
        }
        _thread_pool.scaling(num_threads);

        // Shared state for coordination
        std::mutex task_mutex;
        std::condition_variable completion_cv;
        std::atomic<size_t> pending_tasks{num_files};

        // Submit file loading tasks with optimized callback
        for (size_t i = 0; i < num_files; ++i) {
            _thread_pool.submit_with_callback(
                [this, file_path = file_paths[i]]() {
                    return preprocess_file(file_path);
                },
                [this, i, &file_contents, &file_pages, &page_results, &pending_tasks, &completion_cv](
                    std::pair<std::shared_ptr<FileContent>, std::vector<Page>> result) {
                    if (!result.first || result.second.empty()) {
                        // No valid file content or pages, decrement and notify if done
                        if (pending_tasks.fetch_sub(1) == 1) {
                            completion_cv.notify_one();
                        }
                        return;
                    }
                    file_contents[i] = result.first;
                    file_pages[i] = std::move(result.second);

                    size_t num_pages = file_pages[i].size();
                    page_results[i].resize(num_pages);

                    // Batch submit all page tasks
                    pending_tasks.fetch_add(num_pages - 1);
                    for (size_t j = 0; j < num_pages; ++j) {
                        _thread_pool.submit(
                            [this, i, j, page = file_pages[i][j], &page_results, &pending_tasks, &completion_cv]() {
                                page_results[i][j] = process_page(page);
                                // Only notify when all tasks complete
                                if (pending_tasks.fetch_sub(1) == 1) {
                                    completion_cv.notify_one();
                                }
                            });
                    }
                });
        }

        // Wait for all tasks to complete
        {
            std::unique_lock<std::mutex> lock(task_mutex);
            completion_cv.wait(lock, [&pending_tasks] {
                return pending_tasks.load() == 0;
            });
        }

        // Collect results from file_contents and page_results
        std::vector<ProcessingResult> results(num_files);
        for (size_t i = 0; i < num_files; ++i) {
            const auto &file_path = file_paths[i];
            const auto &file_content = file_contents[i];

            results[i].file_path = file_path;
            if (!file_content) {
                results[i].ok = false;
                results[i].err_msg = "Failed to load file content";
                continue;
            }

            // Check if any page processing failed
            bool has_error = false;
            std::string error_messages;
            size_t total_replacements = 0;

            for (const auto &page_result : page_results[i]) {
                if (!page_result.ok) {
                    has_error = true;
                    if (!error_messages.empty()) {
                        error_messages += "; ";
                    }
                    error_messages += "Page " + std::to_string(page_result.page_id) + ": " + page_result.err_msg;
                } else {
                    total_replacements += page_result.n_rep;
                }
            }

            if (has_error) {
                results[i].ok = false;
                results[i].err_msg = error_messages;
                results[i].n_rep = total_replacements;
            } else {
                results[i].ok = true;
                results[i].n_rep = total_replacements;
            }
        }

        return results;
    }

    std::shared_ptr<FileContent> FileProcessor::load_file_content(const std::string &file_path) const {
        try {
            if (!is_text_file(file_path)) {
                return nullptr;
            }

            // Get file size for pre-allocation hint
            struct stat stat_buf;
            size_t file_size = 0;
            if (stat(file_path.c_str(), &stat_buf) == 0) {
                file_size = static_cast<size_t>(stat_buf.st_size);
            }

            std::wifstream input_file(file_path);
            if (!input_file) {
                return nullptr;
            }
            input_file.imbue(std::locale(std::locale(), new std::codecvt_utf8<wchar_t>));

            text_t content;
            if (file_size > 0) {
                content.reserve(file_size);
            }

            text_t line;
            bool first_line = true;

            while (std::getline(input_file, line)) {
                if (!first_line) {
                    content += L'\n';
                }
                content += line;
                first_line = false;
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
        const auto &protected_intervals = fc_ptr->protected_intervals;
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

                if (line_break_pos != text_t::npos && line_break_pos > search_start) {
                    end_pos = line_break_pos + 1;
                } else {
                    size_t space_pos = content.rfind(L' ', end_pos);
                    if (space_pos != text_t::npos && space_pos > search_start) {
                        end_pos = space_pos + 1;
                    }
                }

                // NOTE: Check if end_pos falls inside a protected region
                // If so, adjust to avoid splitting the protected region
                for (const auto &interval : protected_intervals) {
                    // If end_pos is inside a protected region [start_first, end_last]
                    if (interval.start_first < end_pos && end_pos <= interval.end_last) {
                        // Case 1: Protected region starts before end_pos
                        // Move end_pos to before the protected region starts
                        if (interval.start_first > start_pos) {
                            end_pos = interval.start_first;
                        } else {
                            // Case 2: Protected region started before this page
                            // Move end_pos to after the protected region ends
                            end_pos = interval.skip_to();
                        }
                        break;
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

    std::pair<std::shared_ptr<FileContent>, std::vector<Page>> FileProcessor::preprocess_file(const std::string &file_path) const {
        auto file_content = load_file_content(file_path);
        if (file_content) {
            // Build global protected intervals for the entire file
            file_content->protected_intervals = _ac_automaton.build_global_protected_intervals(file_content->content);

            auto pages = create_pages(file_content);
            return std::make_pair(file_content, std::move(pages));
        } else {
            return std::make_pair(nullptr, std::vector<Page>{});
        }
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
            result.n_rep = apply_replace(result.processed_content,
                                         page.start_pos,
                                         page.f_ptr->protected_intervals);

            page.f_ptr->total_replacements.fetch_add(result.n_rep);

            {
                std::lock_guard<std::mutex> lock(page.f_ptr->processed_mutex);
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

            text_t complete_content;
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

    size_t FileProcessor::apply_replace(text_t &text, const size_t page_offset,
                                        const GlobalProtectedIntervals &global_intervals) const {
        return _ac_automaton.apply_replace(text, page_offset, global_intervals);
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
