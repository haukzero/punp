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

        // Replace format: REPLACE(FROM "...", TO "...");
        bool Parser::parse_replace() {
            size_t current_line = _current_token.line;
            auto kwargs_keys = kwargs_keys_t({"FROM", "TO"});
            bool is_valid = true;
            auto kwargs = parse_args(kwargs_keys, is_valid);

            if (!is_valid)
                return false;

            for (const auto &key : kwargs_keys) {
                if (kwargs.find(key) == kwargs.end()) {
                    error("Missing argument '", key, "' in REPLACE at ", _file_path, ':', current_line);
                    return false;
                }
            }

            if (!expect(TokenType::TOKEN_RPAREN)) {
                error("Expected ')' after REPLACE arguments at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            if (!expect(TokenType::TOKEN_SEMICOLON)) {
                error("Expected ';' after REPLACE statement at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            _rep_map_ptr->emplace(to_tstr(kwargs["FROM"]), to_tstr(kwargs["TO"]));
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

            if (kwargs.find("FROM") == kwargs.end()) {
                error("Missing argument 'FROM' in DEL at ", _file_path, ':', current_line);
                return false;
            }

            if (!expect(TokenType::TOKEN_RPAREN)) {
                error("Expected ')' after DEL arguments at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            if (!expect(TokenType::TOKEN_SEMICOLON)) {
                error("Expected ';' after DEL statement at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            if (_rep_map_ptr->erase(to_tstr(kwargs["FROM"])) == 0) {
                warn("No rule found to erase for '", kwargs["FROM"],
                     "' at ", _file_path, ':', current_line);
            }
            return true;
        }

        // Clear format: CLEAR();
        bool Parser::parse_clear() {
            if (!expect(TokenType::TOKEN_RPAREN)) {
                error("Expected ')' after CLEAR at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            if (!expect(TokenType::TOKEN_SEMICOLON)) {
                error("Expected ';' after CLEAR statement at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

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

            for (const auto &key : kwargs_keys) {
                if (kwargs.find(key) == kwargs.end()) {
                    error("Missing argument '", key, "' in PROTECT at ", _file_path, ':', current_line);
                    return false;
                }
            }

            if (!expect(TokenType::TOKEN_RPAREN)) {
                error("Expected ')' after PROTECT arguments at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            if (!expect(TokenType::TOKEN_SEMICOLON)) {
                error("Expected ';' after PROTECT statement at ",
                      _file_path,
                      ':', _current_token.line,
                      ':', _current_token.column);
                return false;
            }

            _protected_regions_ptr->emplace_back(
                ProtectedRegion{
                    to_tstr(kwargs["START_MARKER"]),
                    to_tstr(kwargs["END_MARKER"])});
            return true;
        }

    } // namespace config_parser
} // namespace punp
