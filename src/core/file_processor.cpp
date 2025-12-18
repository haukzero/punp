#include "core/file_processor.h"

#include "base/color_print.h"
#include "base/common.h"
#include "base/thread_pool/thread_pool.h"
#include "base/types.h"
#include "config/config_manager.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstddef>
#include <fstream>

namespace punp {

    FileProcessor::FileProcessor(const ConfigManager &config_manager)
        : _thread_pool(ThreadPool(1)),
          _writeback_stop(false) {

        // Initialize the AC automaton with the replacement map
        _ac_automaton.build_from_map(*config_manager.replacement_map());
        // Save protected regions for building protected intervals during file processing
        _protected_regions = *config_manager.protected_regions();
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

    std::vector<ProcessingResult> FileProcessor::process_files(const FileProcessorConfig &config) {

        if (config.file_paths.empty()) {
            return {};
        }
        size_t num_files = config.file_paths.size();

        std::vector<std::shared_ptr<FileContent>> file_contents(num_files);
        std::vector<std::vector<Page>> file_pages(num_files);
        std::vector<std::vector<PageResult>> page_results(num_files);

        size_t num_threads = config.max_threads;
        if (num_threads == 0) {
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
                [this, file_path = config.file_paths[i]]() {
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
            const auto &file_path = config.file_paths[i];
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
            input_file.imbue(std::locale(std::locale(), new char_convert_t));

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
        const auto &protected_intervals = fc_ptr->protected_interval;
        size_t content_size = content.size();

        size_t page_id = 0;
        size_t start_pos = 0;
        size_t interval_idx = 0; // Index for tracking current protected interval

        while (start_pos < content_size) {
            // Check if there's a protected region ahead
            bool has_protected_ahead = false;
            const ProtectedInterval *next_interval = nullptr;

            if (interval_idx < protected_intervals.size()) {
                next_interval = &protected_intervals[interval_idx];
                has_protected_ahead = (next_interval->start_first >= start_pos);
            }

            // Check if we're at the start of a protected region
            if (has_protected_ahead && start_pos == next_interval->start_first) {
                // Create a protected page covering the entire protected region
                size_t end_pos = next_interval->skip_to();
                Page protected_page(fc_ptr, page_id++, start_pos, end_pos);
                protected_page.is_protected = true;
                pages.emplace_back(std::move(protected_page));

                start_pos = end_pos;
                interval_idx++; // Move to next protected interval
            } else {
                // Create a regular page
                size_t end_pos = std::min(start_pos + PageConfig::SIZE, content_size);

                // Ensure we don't cross into a protected region
                if (has_protected_ahead && end_pos > next_interval->start_first) {
                    // Adjust end_pos to stop before the protected region
                    end_pos = next_interval->start_first;
                }

                // Try to find a good boundary (line break or space) only if not at protected boundary
                if (end_pos < content_size && (!has_protected_ahead || end_pos < next_interval->start_first)) {
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

                    // Re-check: ensure we still don't cross into protected region after boundary adjustment
                    if (has_protected_ahead && end_pos > next_interval->start_first) {
                        end_pos = next_interval->start_first;
                    }
                }

                Page page(fc_ptr, page_id++, start_pos, end_pos);
                pages.emplace_back(std::move(page));
                start_pos = end_pos;
            }
        }

        fc_ptr->ref_cnt = static_cast<int>(pages.size());
        fc_ptr->processed_pages.resize(pages.size());

        return pages;
    }

    std::pair<std::shared_ptr<FileContent>, std::vector<Page>> FileProcessor::preprocess_file(const std::string &file_path) const {
        auto file_content = load_file_content(file_path);
        if (file_content) {
            // Build global protected intervals for the entire file
            file_content->protected_interval = build_protected_intervals(file_content->content);

            auto pages = create_pages(file_content);
            return std::make_pair(file_content, std::move(pages));
        } else {
            return std::make_pair(nullptr, std::vector<Page>{});
        }
    }

    /// Build global protected intervals for entire file content
    /// This function scans the text and identifies all protected regions based on
    /// start/end marker pairs. It's part of the file processing logic, not AC automaton.
    ProtectedIntervals FileProcessor::build_protected_intervals(const text_t &text) const {
        ProtectedIntervals intervals;

        if (_protected_regions.empty() || text.empty()) {
            return intervals;
        }

        // Single-pass scan through text
        size_t pos = 0, text_len = text.length();
        while (pos < text_len) {
            // Early exit if remaining text is shorter than shortest start marker
            if (text_len - pos < _protected_regions.front().first.length()) {
                break;
            }

            // Try to match any start marker at current position
            bool found_start = false;
            const text_t *matched_start = nullptr;
            const text_t *matched_end = nullptr;
            size_t start_pos = pos;

            for (const auto &region_ptrs : _protected_regions) {
                const text_t &start_marker = region_ptrs.first;

                if (pos + start_marker.length() <= text_len) {
                    if (view_t(text.data() + pos, start_marker.length()) == view_t(start_marker)) {
                        matched_start = &start_marker;
                        matched_end = &region_ptrs.second;
                        found_start = true;
                        break;
                    }
                }
            }

            if (found_start) {
                // Found a start marker, now search for corresponding end marker
                size_t end_search_pos = start_pos + matched_start->length();
                size_t end_begin = text.find(*matched_end, end_search_pos);

                if (end_begin == text_t::npos) {
                    break;
                }

                size_t end_last = end_begin + matched_end->length() - 1;
                intervals.emplace_back(start_pos, end_last,
                                       matched_start->length(), matched_end->length());
                pos = end_begin + matched_end->length();
            } else {
                // No start marker at current position, move forward
                ++pos;
            }
        }

        // Sort intervals by start position for efficient lookup
        // Note: We don't merge overlapping intervals here because we need
        // to preserve the exact marker positions for skipping logic
        std::sort(intervals.begin(), intervals.end(),
                  [](const ProtectedInterval &a, const ProtectedInterval &b) {
                      return a.start_first < b.start_first;
                  });

        return intervals;
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

            // If this page is protected, just return the original content
            if (page.is_protected) {
                page.f_ptr->processed_pages[page.pid] = result.processed_content;

                int remaining = page.f_ptr->ref_cnt.fetch_sub(1) - 1;
                if (remaining == 0) {
                    size_t total_reps = page.f_ptr->total_replacements.load();
                    const_cast<FileProcessor *>(this)->notify_writeback(page.f_ptr, total_reps);
                }
                return result;
            }

            // Apply replacements (no need to pass protected intervals anymore)
            result.n_rep = apply_replace(result.processed_content);

            page.f_ptr->total_replacements.fetch_add(result.n_rep);

            page.f_ptr->processed_pages[page.pid] = result.processed_content;

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

            std::wofstream output_file(file_content->filename);
            if (!output_file) {
                error("Cannot open file for writing: ", file_content->filename);
                return false;
            }

            output_file.imbue(std::locale(std::locale(), new char_convert_t));
            for (const auto &page_content : file_content->processed_pages) {
                output_file << page_content;
            }
            output_file << '\n';
            output_file.close();

            return true;

        } catch (const std::exception &e) {
            error("Writing file ", file_content->filename, ": ", e.what());
            return false;
        }
    }

    size_t FileProcessor::apply_replace(text_t &text) const {
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

} // namespace punp << std::endl
