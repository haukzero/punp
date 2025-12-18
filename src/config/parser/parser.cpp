#include "config/parser/parser.h"

#include "base/color_print.h"
#include "base/types.h"
#include "config/parser/token.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace punp {
    namespace config_parser {

        void Parser::advance() {
            _current_token = _peek_token;
            _peek_token = _lexer.next_token();
        }

        bool Parser::expect(const TokenType &type) {
            if (_current_token.type == type) {
                advance();
                return true;
            }
            return false;
        }

        void Parser::parse() {
            while (_current_token.type != TokenType::TOKEN_EOF) {
                parse_statement();
            }
        }

        void Parser::parse_statement() {
            if (_current_token.type != TokenType::TOKEN_IDENT) {
                error("Expected statement at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                // try to skip to next semicolon
                while (_current_token.type != TokenType::TOKEN_SEMICOLON && _current_token.type != TokenType::TOKEN_EOF) {
                    advance();
                }
                if (_current_token.type == TokenType::TOKEN_SEMICOLON) {
                    advance();
                } else {
                    // EOF reached
                    error("Unexpected end of file while parsing statement at ",
                          _file_path,
                          ':', _current_token.line,
                          ':', _current_token.column);
                    return;
                }
            }

            std::string keyword = _current_token.value;
            to_upper(keyword);

            if (_peek_token.type != TokenType::TOKEN_LPAREN) {
                error("Expected '(' after ", keyword,
                      " at ", _file_path,
                      ':', _peek_token.line,
                      ':', _peek_token.column,
                      " (Found: ", _peek_token.value, ")");
                // skip to next semicolon
                while (_current_token.type != TokenType::TOKEN_SEMICOLON &&
                       _current_token.type != TokenType::TOKEN_EOF) {
                    advance();
                }
                if (_current_token.type == TokenType::TOKEN_SEMICOLON) {
                    advance();
                } else {
                    // EOF reached
                    error("Unexpected end of file while parsing statement at ",
                          _file_path,
                          ':', _current_token.line,
                          ':', _current_token.column);
                }
                return;
            }

            advance();
            advance();

            // NOTE: parse based on keyword
            bool success = true;
            auto it = _parse_func_map.find(keyword);
            if (it != _parse_func_map.end()) {
                success = (this->*(it->second))();
            } else {
                error("Unknown command '", keyword,
                      "' at ", _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                success = false;
            }

            if (!success) {
                // If parsing failed, we might be at a semicolon or EOF
                if (_current_token.type == TokenType::TOKEN_SEMICOLON) {
                    advance();
                } else {
                    // Try to recover to next semicolon
                    while (_current_token.type != TokenType::TOKEN_SEMICOLON &&
                           _current_token.type != TokenType::TOKEN_EOF) {
                        advance();
                    }
                    if (_current_token.type == TokenType::TOKEN_SEMICOLON) {
                        advance();
                    }
                }
            }
        }

        /// Parse keyword arguments from the token stream
        ///
        /// Parses a comma-separated list of key-value pairs in the format:
        /// KEY "value", KEY "value", ...
        ///
        /// The function expects the current token to be at the first argument
        /// (after the opening parenthesis) and stops when it encounters a
        /// closing parenthesis ')' or EOF.
        ///
        /// @param kwargs_keys A vector of allowed/expected argument keys (case-insensitive).
        ///                    Keys not in this list will trigger an error but won't stop parsing.
        /// @param is_valid [out] Set to false if any parsing error occurs, true otherwise.
        ///                 The caller should check this flag to determine if parsing succeeded.
        ///
        /// @return A map of parsed key-value pairs (keys are converted to uppercase).
        ///         Returns an empty or partial map if is_valid is set to false.
        ///
        /// @note
        /// - All keys are automatically converted to uppercase for case-insensitive matching
        /// - Duplicate keys are ignored with a warning, only the first occurrence is kept
        /// - Trailing commas are not allowed and will trigger an error
        /// - Unknown keys (not in kwargs_keys) trigger an error but don't stop parsing
        /// - The function advances tokens internally; on success, current token will be at ')'
        ///
        /// @example
        /// // For input: FROM "source", TO "target"
        /// // Returns: {"FROM": "source", "TO": "target"}
        Parser::kwargs_t Parser::parse_args(const kwargs_keys_t &kwargs_keys, bool &is_valid) {
            Parser::kwargs_t kwargs;
            kwargs.reserve(kwargs_keys.size());
            bool is_first = true;
            is_valid = true;

            while (_current_token.type != TokenType::TOKEN_RPAREN &&
                   _current_token.type != TokenType::TOKEN_EOF) {

                // Check for unexpected semicolon (likely missing ')')
                if (_current_token.type == TokenType::TOKEN_SEMICOLON) {
                    error("Unexpected token ';' at ",
                          _file_path,
                          ':', _current_token.line,
                          ':', _current_token.column,
                          ". Expected ')'.");
                    is_valid = false;
                    return kwargs;
                }

                if (!is_first) {
                    if (_current_token.type == TokenType::TOKEN_COMMA) {
                        advance();
                        if (_current_token.type == TokenType::TOKEN_RPAREN) {
                            error("Trailing comma is not allowed at ",
                                  _file_path,
                                  ':', _current_token.line,
                                  ':', _current_token.column);
                            is_valid = false;
                            return kwargs;
                        }
                    } else {
                        error("Expected ',' between arguments at ",
                              _file_path,
                              ':', _current_token.line,
                              ':', _current_token.column);
                        is_valid = false;
                        return kwargs;
                    }
                }

                if (_current_token.type != TokenType::TOKEN_IDENT) {
                    error("Expected argument key at ",
                          _file_path,
                          ':', _current_token.line,
                          ':', _current_token.column,
                          " (Got: '", _current_token.value, "')");
                    is_valid = false;
                    return kwargs;
                }

                std::string key = _current_token.value;
                to_upper(key);
                advance();

                if (_current_token.type != TokenType::TOKEN_STRING) {
                    error("Expected string value for key '", key,
                          "' at ", _file_path,
                          ':', _current_token.line,
                          ':', _current_token.column,
                          " (Got: '", _current_token.value, "')");
                    is_valid = false;
                    return kwargs;
                }

                std::string value = _current_token.value;
                advance();

                bool is_known_key = false;
                for (const auto &k : kwargs_keys) {
                    if (k == key) {
                        is_known_key = true;
                        break;
                    }
                }

                if (is_known_key) {
                    if (kwargs.find(key) != kwargs.end()) {
                        warn("Duplicate key '", key, "' ignored.");
                    } else {
                        kwargs[key] = value;
                    }
                } else {
                    error("Unknown argument key '", key,
                          "' at ", _file_path,
                          ':', _current_token.line,
                          ':', _current_token.column);
                }

                is_first = false;
            }

            if (_current_token.type == TokenType::TOKEN_EOF) {
                error("Unexpected end of file. Expected ')' at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                is_valid = false;
                return kwargs;
            }

            return kwargs;
        }

        void Parser::to_upper(std::string &str) const {
            std::transform(str.begin(), str.end(), str.begin(),
                           [](unsigned char c) { return std::toupper(c); });
        }

        text_t Parser::to_tstr(const std::string &str) const {
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

    } // namespace config_parser
} // namespace punp

// NOTE: This section implements the specific parsing method for each rule.
namespace punp {
    namespace config_parser {

#define PUNP_CHECK_REQUIRED_ARGS(kwargs, keys, cmd_name, line)                                      \
    do {                                                                                            \
        for (const auto &key : keys) {                                                              \
            if (kwargs.find(key) == kwargs.end()) {                                                 \
                error("Missing argument '", key, "' in ", cmd_name, " at ", _file_path, ':', line); \
                return false;                                                                       \
            }                                                                                       \
        }                                                                                           \
    } while (0)

#define PUNP_EXPECT_RPAREN(cmd_name)                                                 \
    do {                                                                             \
        if (!expect(TokenType::TOKEN_RPAREN)) {                                      \
            error("Expected ')' after ", cmd_name, " arguments at ",                 \
                  _file_path, ':', _current_token.line, ':', _current_token.column); \
            return false;                                                            \
        }                                                                            \
    } while (0)

#define PUNP_EXPECT_SEMICOLON(cmd_name)                                              \
    do {                                                                             \
        if (!expect(TokenType::TOKEN_SEMICOLON)) {                                   \
            error("Expected ';' after ", cmd_name, " statement at ",                 \
                  _file_path, ':', _current_token.line, ':', _current_token.column); \
            return false;                                                            \
        }                                                                            \
    } while (0)

#define PUNP_FINALIZE_PARSE(kwargs, keys, cmd_name, line)       \
    do {                                                        \
        PUNP_CHECK_REQUIRED_ARGS(kwargs, keys, cmd_name, line); \
        PUNP_EXPECT_RPAREN(cmd_name);                           \
        PUNP_EXPECT_SEMICOLON(cmd_name);                        \
    } while (0)

#define PUNP_FINALIZE_PARSE_NO_CHECK(cmd_name) \
    do {                                       \
        PUNP_EXPECT_RPAREN(cmd_name);          \
        PUNP_EXPECT_SEMICOLON(cmd_name);       \
    } while (0)

        // Replace format: REPLACE(FROM "...", TO "...");
        bool Parser::parse_replace() {
            size_t current_line = _current_token.line;
            auto kwargs_keys = kwargs_keys_t({"FROM", "TO"});
            bool is_valid = true;
            auto kwargs = parse_args(kwargs_keys, is_valid);

            if (!is_valid)
                return false;

            PUNP_FINALIZE_PARSE(kwargs, kwargs_keys, "REPLACE", current_line);

            _rep_map_ptr->insert_or_assign(to_tstr(kwargs["FROM"]), to_tstr(kwargs["TO"]));
            return true;
        }

        // Del format: DEL(FROM "...");
        bool Parser::parse_del() {
            size_t current_line = _current_token.line;
            auto kwargs_keys = kwargs_keys_t({"FROM"});
            bool is_valid = true;
            auto kwargs = parse_args(kwargs_keys, is_valid);

            if (!is_valid)
                return false;

            PUNP_FINALIZE_PARSE(kwargs, kwargs_keys, "DEL", current_line);

            if (_rep_map_ptr->erase(to_tstr(kwargs["FROM"])) == 0) {
                warn("No rule found to erase for '", kwargs["FROM"],
                     "' at ", _file_path, ':', current_line);
            }
            return true;
        }

        // Clear format: CLEAR();
        bool Parser::parse_clear() {
            PUNP_FINALIZE_PARSE_NO_CHECK("CLEAR");

            _rep_map_ptr->clear();
            return true;
        }

        // Protect format: PROTECT(START_MARKER "...", END_MARKER "...");
        bool Parser::parse_protect() {
            size_t current_line = _current_token.line;
            auto kwargs_keys = kwargs_keys_t({"START_MARKER", "END_MARKER"});
            bool is_valid = true;
            auto kwargs = parse_args(kwargs_keys, is_valid);

            if (!is_valid)
                return false;

            PUNP_FINALIZE_PARSE(kwargs, kwargs_keys, "PROTECT", current_line);

            _protected_regions_ptr->emplace_back(
                ProtectedRegion{
                    to_tstr(kwargs["START_MARKER"]),
                    to_tstr(kwargs["END_MARKER"])});
            return true;
        }

        // Protect content format: PROTECT_CONTENT(CONTENT "...");
        bool Parser::parse_protect_content() {
            size_t current_line = _current_token.line;
            auto kwargs_keys = kwargs_keys_t({"CONTENT"});
            bool is_valid = true;
            auto kwargs = parse_args(kwargs_keys, is_valid);

            if (!is_valid)
                return false;

            PUNP_FINALIZE_PARSE(kwargs, kwargs_keys, "PROTECT_CONTENT", current_line);

            _protected_regions_ptr->emplace_back(
                ProtectedRegion{
                    to_tstr(kwargs["CONTENT"]),
                    to_tstr("")});
            return true;
        }

#undef PUNP_CHECK_REQUIRED_ARGS
#undef PUNP_EXPECT_RPAREN
#undef PUNP_EXPECT_SEMICOLON
#undef PUNP_FINALIZE_PARSE
#undef PUNP_FINALIZE_PARSE_NO_CHECK

    } // namespace config_parser
} // namespace punp
