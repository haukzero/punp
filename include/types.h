#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace punp {

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

    // File content structure
    struct FileContent {
        std::string filename;
        std::wstring content;
        std::atomic<int> ref_cnt{0};
        std::vector<std::wstring> processed_pages;
        std::atomic<size_t> total_replacements{0};
        std::mutex processed_mutex;

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

    // Writeback notification
    struct WritebackNotification {
        std::shared_ptr<FileContent> file_content;
        size_t total_replacements;

        WritebackNotification(std::shared_ptr<FileContent> fc, size_t reps)
            : file_content(fc), total_replacements(reps) {}
    };

} // namespace punp
