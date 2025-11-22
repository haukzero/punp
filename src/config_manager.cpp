#include "config_manager.h"
#include "common.h"
#include "types.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace punp {

    namespace tokenizer {

        enum class TokenType {
            KEYWORD,
            STRING,
            COMMA,
        };

        struct Token {
            TokenType type;
            std::string value;
        };

        std::vector<Token> tokenize(const std::string &input) {
            std::vector<Token> tokens;
            bool in_string = false;
            std::string current;
            for (size_t i = 0; i < input.length(); ++i) {
                char c = input[i];
                if (in_string) {
                    if (c == '"') {
                        if (i == 0 || input[i - 1] != '\\') {
                            in_string = false;
                            tokens.push_back({TokenType::STRING, current});
                            current.clear();
                        } else {
                            current.pop_back(); // Remove the escape character
                            current += c;
                        }
                    } else {
                        current += c;
                    }
                } else {
                    if (std::isspace(static_cast<unsigned char>(c))) {
                        if (!current.empty()) {
                            tokens.push_back({TokenType::KEYWORD, current});
                            current.clear();
                        }
                        continue;
                    }
                    if (c == '"') {
                        if (!current.empty()) {
                            tokens.push_back({TokenType::KEYWORD, current});
                            current.clear();
                        }
                        in_string = true;
                    } else if (c == ',') {
                        if (!current.empty()) {
                            tokens.push_back({TokenType::KEYWORD, current});
                            current.clear();
                        }
                        tokens.push_back({TokenType::COMMA, ","});
                    } else {
                        current += c;
                    }
                }
            }
            if (!current.empty())
                tokens.push_back({TokenType::KEYWORD, current});
            return tokens;
        }
    }

    // static std::unordered_map<std::string, typename Tp>
    bool ConfigManager::load(bool verbose) {
        auto config_files = find_files();

        if (config_files.empty()) {
            std::cerr << Colors::RED << "Error: No configuration files found.\n";
            std::cerr << "Please create a '" << RuleFile::NAME << "' file in:\n";
            std::cerr << "  - Current directory, or\n";
            std::cerr << "  - User config directory (" << StoreDir::CONFIG_DIR << ")\n"
                      << Colors::RESET;
            return false;
        }

        bool ok = false;
        for (const auto &cf : config_files) {
            if (parse(cf)) {
                if (verbose) {
                    std::cout << "Loaded config from: " << cf << '\n';
                }
                ok = true;
            } else if (verbose) {
                std::cout << Colors::YELLOW << "Skipped config file: " << cf << " (not found or invalid)" << '\n'
                          << Colors::RESET;
            }
        }

        if (verbose && ok) {
            std::cout << "Total replacement rules loaded: " << _rep_map.size() << '\n';
            std::cout << "Total protected rules loaded: " << _protected_regions.size() << '\n';
        }

        return ok;
    }

    std::vector<std::string> ConfigManager::find_files() const {
        std::vector<std::string> config_files;

        // Check user config directory first (lower priority)
        config_files.emplace_back(StoreDir::CONFIG_DIR + RuleFile::NAME);

        // Check current directory (higher priority)
        config_files.emplace_back(RuleFile::NAME);

        return config_files;
    }

    bool ConfigManager::parse(const std::string &file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        std::string stmt;
        int line_num = 1;
        int stmt_start_line = 1;
        bool in_string = false;
        bool in_comment = false;
        bool stmt_started = false;
        int paren_balance = 0;
        bool expecting_semicolon = false;

        size_t rules_count_before = _rep_map.size() + _protected_regions.size();

        for (size_t i = 0; i <= content.size(); ++i) {
            bool is_eof = (i == content.size());
            char c = is_eof ? '\0' : content[i];

            if (!is_eof) {
                if (c == '\n') {
                    line_num++;
                    if (in_comment)
                        in_comment = false;
                }

                if (in_comment)
                    continue;

                if (in_string) {
                    if (c == '"' && (i == 0 || content[i - 1] != '\\')) {
                        in_string = false;
                    }
                    stmt += c;
                    continue;
                }

                if (c == '/' && i + 1 < content.size() && content[i + 1] == '/') {
                    in_comment = true;
                    i++;
                    continue;
                }

                if (c == '"') {
                    in_string = true;
                    if (!stmt_started) {
                        stmt_started = true;
                        stmt_start_line = line_num;
                    }
                    stmt += c;
                    continue;
                }
            } else if (in_comment || in_string) {
                break;
            }

            bool missing_semicolon = false;
            if (expecting_semicolon) {
                if (is_eof || (c != ';' && !std::isspace(static_cast<unsigned char>(c)))) {
                    missing_semicolon = true;
                }
            } else if (is_eof && stmt_started) {
                bool has_content = false;
                for (char sc : stmt)
                    if (!std::isspace(static_cast<unsigned char>(sc)))
                        has_content = true;
                if (has_content)
                    missing_semicolon = true;
            }

            if (missing_semicolon) {
                std::cerr << Colors::YELLOW << "Warning: Missing `;` after statement at "
                          << file_path << ":" << stmt_start_line
                          << ". Rule ignored.\n"
                          << Colors::RESET;
                stmt.clear();
                stmt_started = false;
                paren_balance = 0;
                expecting_semicolon = false;
                if (is_eof)
                    break;
            }

            if (is_eof)
                break;

            if (c == ';') {
                if (paren_balance == 0) {
                    process_statement(stmt, file_path, stmt_start_line);
                    stmt.clear();
                    stmt_started = false;
                    expecting_semicolon = false;
                } else {
                    stmt += c;
                }
                continue;
            }

            if (c == '(') {
                paren_balance++;
            } else if (c == ')') {
                if (paren_balance > 0)
                    paren_balance--;
                if (paren_balance == 0 && stmt_started) {
                    expecting_semicolon = true;
                }
            }

            if (!std::isspace(static_cast<unsigned char>(c))) {
                if (!stmt_started) {
                    stmt_started = true;
                    stmt_start_line = line_num;
                }
                stmt += c;
            } else if (stmt_started) {
                stmt += ' ';
            }
        }

        return (_rep_map.size() + _protected_regions.size()) > rules_count_before || rules_count_before > 0;
    }

    void ConfigManager::process_statement(const std::string &stmt, const std::string &file_path, int lno) {
        std::string s = stmt;
        // Trim
        size_t first = s.find_first_not_of(" ");
        if (first == std::string::npos)
            return;
        s = s.substr(first);
        size_t last = s.find_last_not_of(" ");
        if (last != std::string::npos)
            s = s.substr(0, last + 1);

        size_t open_paren = s.find('(');
        size_t close_paren = s.rfind(')');

        if (open_paren == std::string::npos || close_paren == std::string::npos || close_paren < open_paren) {
            std::cerr << Colors::YELLOW << "Warning: Invalid syntax at " << file_path << ":" << lno << ": " << s << '\n'
                      << Colors::RESET;
            return;
        }

        std::string command = s.substr(0, open_paren);
        // Trim command
        while (!command.empty() && command.back() == ' ')
            command.pop_back();
        to_upper(command);

        std::string args = s.substr(open_paren + 1, close_paren - open_paren - 1);

        if (command == "REPLACE") {
            parse_replace(args, file_path, lno);
        } else if (command == "DEL") {
            parse_del(args, file_path, lno);
        } else if (command == "PROTECT") {
            parse_protect(args, file_path, lno);
        } else if (command == "CLEAR") {
            parse_clear();
        } else {
            std::cerr << Colors::YELLOW << "Warning: Unknown command '" << command << "' at " << file_path << ":" << lno << '\n'
                      << Colors::RESET;
        }
    }

    bool ConfigManager::parse_replace(const std::string &args, const std::string &file_path, int lno) {
        auto tokens = tokenizer::tokenize(args);
        std::string from_str, to_str;
        bool has_from = false, has_to = false;

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].type == tokenizer::TokenType::KEYWORD) {
                to_upper(tokens[i].value);
                if (tokens[i].value == "FROM") {
                    if (i + 1 < tokens.size() && tokens[i + 1].type == tokenizer::TokenType::STRING) {
                        from_str = tokens[i + 1].value;
                        has_from = true;
                        i++;
                    }
                } else if (tokens[i].value == "TO") {
                    if (i + 1 < tokens.size() && tokens[i + 1].type == tokenizer::TokenType::STRING) {
                        to_str = tokens[i + 1].value;
                        has_to = true;
                        i++;
                    }
                }
            }
        }

        if (!has_from || !has_to) {
            std::cerr << Colors::YELLOW << "Warning: REPLACE requires FROM and TO at " << file_path << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        if (from_str == to_str) {
            std::cerr << Colors::YELLOW << "Warning: Skipping rule with identical 'from' and 'to' at "
                      << file_path << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        _rep_map[to_tstr(from_str)] = to_tstr(to_str);
        return true;
    }

    bool ConfigManager::parse_del(const std::string &args, const std::string &file_path, int lno) {
        auto tokens = tokenizer::tokenize(args);
        if (tokens.size() == 1 && tokens[0].type == tokenizer::TokenType::STRING) {
            if (_rep_map.erase(to_tstr(tokens[0].value)) == 0) {
                std::cerr << Colors::YELLOW << "Warning: No rule found to erase at " << file_path
                          << ":" << lno << '\n'
                          << Colors::RESET;
            }
            return true;
        }
        std::cerr << Colors::YELLOW << "Warning: DEL requires a single string argument at " << file_path << ":" << lno << '\n'
                  << Colors::RESET;
        return false;
    }

    bool ConfigManager::parse_protect(const std::string &args, const std::string &file_path, int lno) {
        auto tokens = tokenizer::tokenize(args);
        std::string start_str, end_str;
        bool has_start = false, has_end = false;

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].type == tokenizer::TokenType::KEYWORD) {
                to_upper(tokens[i].value);
                if (tokens[i].value == "START_MARKER") {
                    if (i + 1 < tokens.size() && tokens[i + 1].type == tokenizer::TokenType::STRING) {
                        start_str = tokens[i + 1].value;
                        has_start = true;
                        i++;
                    }
                } else if (tokens[i].value == "END_MARKER") {
                    if (i + 1 < tokens.size() && tokens[i + 1].type == tokenizer::TokenType::STRING) {
                        end_str = tokens[i + 1].value;
                        has_end = true;
                        i++;
                    }
                }
            }
        }

        if (!has_start || !has_end) {
            std::cerr << Colors::YELLOW << "Warning: PROTECT requires START_MARKER and END_MARKER at " << file_path << ":" << lno << '\n'
                      << Colors::RESET;
            return false;
        }

        _protected_regions.emplace_back(to_tstr(start_str), to_tstr(end_str));

        if (_protected_regions.size() > 1) {
            std::sort(_protected_regions.begin(), _protected_regions.end(),
                      [](const ProtectedRegion &a, const ProtectedRegion &b) {
                          return a.first.length() < b.first.length();
                      });
        }
        return true;
    }

    bool ConfigManager::parse_clear() {
        _rep_map.clear();
        return true;
    }

    void ConfigManager::to_upper(std::string &str) const {
        std::transform(str.begin(), str.end(), str.begin(),
                       [](unsigned char c) { return std::toupper(c); });
    }

    text_t ConfigManager::to_tstr(const std::string &str) const {
        convert_t converter;
        try {
            return converter.from_bytes(str);
        } catch (const std::exception &) {
            // Fallback for invalid UTF-8
            text_t result;
            result.reserve(str.length());
            for (char c : str) {
                result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
            }
            return result;
        }
    }

} // namespace punp
