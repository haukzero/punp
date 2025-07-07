#pragma once

#include "types.h"
#include <atomic>
#include <condition_variable>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace PunctuationProcessor {

    class ConfigManager;
    class ThreadPool;

    class FileProcessor {
    private:
        const ConfigManager &_config_manager;
        PageProcessingConfig _page_config;

        std::queue<WritebackNotification> _writeback_queue;
        std::mutex _writeback_mtx;
        std::condition_variable _writeback_cv;
        std::atomic<bool> _writeback_stop{false};
        std::thread _writeback_thread;

        size_t apply_replace(std::wstring &text) const;
        bool is_text_file(const std::string &file_path) const;

        // Load file content into FileContent structure
        std::shared_ptr<FileContent> load_file_content(const std::string &file_path) const;

        // Create pages from file content
        std::vector<Page> create_pages(std::shared_ptr<FileContent> file_content) const;

        // Process a single page
        PageResult process_page(const Page &page) const;

        void notify_writeback(std::shared_ptr<FileContent> file_content, size_t replacements);
        void writeback_worker();
        bool writeback(const std::shared_ptr<FileContent> &file_content, size_t total_replacements) const;

    public:
        explicit FileProcessor(const ConfigManager &config_manager);
        ~FileProcessor();

        std::vector<ProcessingResult> process_files(
            const std::vector<std::string> &file_paths,
            size_t max_threads = 0) const;
    };

} // namespace PunctuationProcessor
