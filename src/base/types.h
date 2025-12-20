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

    // Protected region interval in text
    struct ProtectedInterval {
        size_t start_first;      // Position of the first char of start marker
        size_t end_last;         // Position of the last char of end marker
        size_t start_marker_len; // Length of start marker
        size_t end_marker_len;   // Length of end marker

        ProtectedInterval(size_t s_first, size_t e_last, size_t s_len, size_t e_len)
            : start_first(s_first), end_last(e_last), start_marker_len(s_len), end_marker_len(e_len) {}

        // Get the position to jump to (right after end marker)
        size_t skip_to() const {
            return end_last + 1;
        }
    };
    using ProtectedIntervals = std::vector<ProtectedInterval>;

    struct RuleConfig {
        bool ignore_global_rule_file = false;
        std::string rule_file_path;
        std::string console_rule;
    };

    struct FileFinderConfig {
        bool recursive = false;
        bool process_hidden = false;
        bool enable_latex_jumping = false;
        std::vector<std::string> patterns;      // File patterns to search
        std::vector<std::string> extensions;    // File extensions to filter
        std::vector<std::string> exclude_paths; // Files/dirs to exclude
    };

    struct FileProcessorConfig {
        std::vector<std::string> file_paths;
        size_t max_threads = 0; // 0 means auto-detect
    };

    struct ProcessingConfig {
        RuleConfig rule_config;
        FileFinderConfig finder_config;
        FileProcessorConfig processor_config;
    };

    // update type
    enum class UpdateType {
        NONE,
        STABLE,
        NIGHTLY,
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
        ProtectedIntervals protected_interval;

        FileContent(const std::string &name, const text_t &data)
            : filename(name), content(data) {}
    };

    // Page data structure
    struct Page {
        std::shared_ptr<FileContent> f_ptr; // Pointer to file content
        size_t pid;                         // Page ID
        size_t start_pos;                   // Start position in file content
        size_t end_pos;                     // End position in file content
        bool is_protected = false;          // If this page is protected, no replacements will be applied

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