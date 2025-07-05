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

        size_t apply_replace(std::wstring &text) const;
        bool is_text_file(const std::string &file_path) const;

    public:
        explicit FileProcessor(const ConfigManager &config_manager);
        ~FileProcessor() = default;

        ProcessingResult process_file(const std::string &file_path) const;

        std::vector<ProcessingResult> process_files(
            const std::vector<std::string> &file_paths,
            size_t max_threads = 0) const;
    };

} // namespace PunctuationProcessor
