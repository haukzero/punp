#pragma once

#include "types.h"
#include <string>
#include <vector>

namespace PunctuationProcessor {

    class ConfigManager;
    class ThreadPool;

    class FileProcessor {
    private:
        // Helper structure for file analysis
        struct FileAnalysis {
            std::string file_path;
            std::wstring content;
            bool is_large_file;
            std::vector<size_t> chunk_boundaries;
        };

        const ConfigManager &_config_manager;
        ChunkProcessingConfig _chunk_config;

        size_t apply_replace(std::wstring &text) const;
        bool is_text_file(const std::string &file_path) const;

        std::vector<size_t> cal_chunk_boundaries(const std::wstring &content, size_t chunk_size) const;
        ChunkResult process_chunk(const std::wstring &chunk_content) const;
        ProcessingResult process_small_file(const std::string &file_path) const;
        bool should_use_chunking(const std::wstring &content, size_t available_threads) const;

        FileAnalysis analyze_file(const std::string &file_path, size_t available_threads) const;
        ProcessingResult merge_chunk_results(const std::string &file_path,
                                             const std::vector<ChunkResult> &chunk_results) const;

    public:
        explicit FileProcessor(const ConfigManager &config_manager);
        ~FileProcessor() = default;

        ProcessingResult process_file(const std::string &file_path) const;

        std::vector<ProcessingResult> process_files(
            const std::vector<std::string> &file_paths,
            size_t max_threads = 0) const;
    };

} // namespace PunctuationProcessor
