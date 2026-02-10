#pragma once

#include "algorithm/aho_corasick.h"
#include "base/thread_pool/thread_pool.h"
#include "base/types.h"

#include <atomic>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace punp {

    class ConfigManager;

    class FileProcessor {
    public:
        explicit FileProcessor(const ConfigManager &config_manager);
        ~FileProcessor();

        std::vector<ProcessingResult> process_files(const FileProcessorConfig &config);

    private:
        AhoCorasick _ac_replace;           // Pattern matching engine for replacements
        std::vector<text_t> _replacements; // Replacement strings corresponding to AC patterns

        AhoCorasick _ac_protected_start;     // Pattern matching engine for protected region start markers
        ProtectedRegions _protected_regions; // Protected region rules (start/end markers)

        ThreadPool _thread_pool; // Thread pool for parallel processing

        /////////////////////////////////////////////////////////////////////////////////////////////////

        std::queue<WritebackNotification> _writeback_queue;
        std::mutex _writeback_mtx;
        std::condition_variable _writeback_cv;
        std::atomic<bool> _writeback_stop{false};
        std::thread _writeback_thread;

        size_t apply_replace(text_t &text) const;
        bool is_text_file(const std::string &file_path) const;

        // Build global protected intervals for entire file content
        ProtectedIntervals build_protected_intervals(const text_t &text) const;
        // Merge adjacent intervals in-place
        void merge_intervals(ProtectedIntervals &intervals) const;

        // Load file content into FileContent structure
        std::shared_ptr<FileContent> load_file_content(const std::string &file_path) const;

        // Create pages from file content
        std::vector<Page> create_pages(std::shared_ptr<FileContent> file_content) const;

        // Pre-process: load file + create pages
        std::pair<std::shared_ptr<FileContent>, std::vector<Page>> preprocess_file(const std::string &file_path) const;

        // Process a single page
        PageResult process_page(const Page &page) const;

        void notify_writeback(std::shared_ptr<FileContent> file_content, size_t replacements);
        void writeback_worker();
        bool writeback(const std::shared_ptr<FileContent> &file_content, size_t total_replacements) const;
    };

} // namespace punp
