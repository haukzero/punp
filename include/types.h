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

    // Configuration for chunk processing
    struct ChunkProcessingConfig {
        size_t min_file_size_for_chunking = 1024 * 1024; // 1MB
        size_t chunk_size = 256 * 1024;                  // 256KB per chunk
        size_t min_chunks_per_file = 2;                  // Minimum chunks to justify parallel processing
    };

    // Internal chunk processing result
    struct ChunkResult {
        std::wstring content;
        size_t n_rep = 0;
        bool ok = true;
        std::string err_msg;
    };

} // namespace PunctuationProcessor
