#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace PunctuationProcessor {

    // Type definitions
    using ReplacementRule = std::pair<std::wstring, std::wstring>;
    using ReplacementMap = std::unordered_map<std::wstring, std::wstring>;

    // Configuration for processing
    struct ProcessingConfig {
        bool recursive = false;
        bool verbose = false;
        size_t max_threads = 0; // 0 means auto-detect
    };

    // File processing result
    struct ProcessingResult {
        std::string file_path;
        bool ok;
        std::string err_msg;
        size_t n_rep = 0;
    };

    // Configuration for page processing
    struct PageProcessingConfig {
        size_t page_size = 256 * 1024; // 256KB per page
    };

    // File content structure
    struct FileContent {
        std::string filename;
        std::wstring content;

        FileContent(const std::string &name, const std::wstring &data)
            : filename(name), content(data) {}
    };

    // Page data structure
    struct Page {
        std::shared_ptr<FileContent> f_ptr; // Pointer to file content
        size_t pid;                         // Page ID
        size_t start_pos;                   // Start position in file content
        size_t end_pos;                     // End position in file content

        Page(std::shared_ptr<FileContent> file_ptr, size_t page_id,
             size_t start, size_t end)
            : f_ptr(file_ptr), pid(page_id), start_pos(start), end_pos(end) {}
    };

    // Page processing result
    struct PageResult {
        std::string file_path;
        size_t page_id;
        std::wstring processed_content;
        size_t n_rep = 0;
        bool ok = true;
        std::string err_msg;
    };

} // namespace PunctuationProcessor
