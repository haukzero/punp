#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace PunctuationProcessor {

    class ConfigManager;
    class ThreadPool;

    class FileProcessor {
    private:
        const ConfigManager &_config_manager;
        PageProcessingConfig _page_config;

        size_t apply_replace(std::wstring &text) const;
        bool is_text_file(const std::string &file_path) const;

        // Load file content into FileContent structure
        std::shared_ptr<FileContent> load_file_content(const std::string &file_path) const;

        // Create pages from file content
        std::vector<Page> create_pages(std::shared_ptr<FileContent> file_content) const;

        // Process a single page
        PageResult process_page(const Page &page) const;

        // Merge page results back into file
        ProcessingResult merge_page_results(const std::string &file_path,
                                            const std::vector<PageResult> &page_results) const;

    public:
        explicit FileProcessor(const ConfigManager &config_manager);
        ~FileProcessor() = default;

        std::vector<ProcessingResult> process_files(
            const std::vector<std::string> &file_paths,
            size_t max_threads = 0) const;
    };

} // namespace PunctuationProcessor
