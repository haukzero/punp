#pragma once

#include <atomic>
#include <codecvt>
#include <locale>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace punp {

    using text_t = std::wstring;
    using char_convert_t = std::codecvt_utf8<wchar_t>;
    using convert_t = std::wstring_convert<char_convert_t>;
    using view_t = std::wstring_view;

    // Type definitions
    using ReplacementRule = std::pair<text_t, text_t>;
    using ReplacementMap = std::unordered_map<text_t, text_t>;

    // Protected region definition (start marker, end marker)
    using ProtectedRegion = std::pair<text_t, text_t>;
    using ProtectedRegions = std::vector<ProtectedRegion>;

    // Global protected interval for entire file
    struct GlobalProtectedInterval {
        size_t start_first;      // Position of the first char of start marker
        size_t end_last;         // Position of the last char of end marker
        size_t start_marker_len; // Length of start marker
        size_t end_marker_len;   // Length of end marker

        GlobalProtectedInterval(size_t s_first, size_t e_last, size_t s_len, size_t e_len)
            : start_first(s_first), end_last(e_last), start_marker_len(s_len), end_marker_len(e_len) {}

        // Get the position to jump to (right after end marker)
        size_t skip_to() const {
            return end_last + 1;
        }
    };
    using GlobalProtectedIntervals = std::vector<GlobalProtectedInterval>;

    // Configuration for processing
    struct ProcessingConfig {
        bool recursive = false;
        bool verbose = false;
        bool process_hidden = false;
        size_t max_threads = 0;                 // 0 means auto-detect
        std::vector<std::string> extensions;    // File extensions to filter
        std::vector<std::string> exclude_paths; // Files/dirs to exclude
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
        text_t content;
        std::atomic<int> ref_cnt{0};
        std::vector<text_t> processed_pages;
        std::atomic<size_t> total_replacements{0};
        std::vector<GlobalProtectedInterval> protected_intervals;

        FileContent(const std::string &name, const text_t &data)
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
        text_t processed_content;
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