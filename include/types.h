#pragma once

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

} // namespace PunctuationProcessor
