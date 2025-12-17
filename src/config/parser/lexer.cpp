#include "config/parser/lexer.h"
#include "config/parser/token.h"

#include <cctype>

namespace punp {
    namespace config_parser {
        Token Lexer::next_token() {
            skip_trivial();

            if (_pos >= _input.size()) {
                return make_token(TokenType::TOKEN_EOF, "");
            }

            char c = peek();
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                return scan_identifier();
            }

            if (c == '"') {
                return scan_string();
            }

            advance();

            switch (c) {
            case '(':
                return make_token(TokenType::TOKEN_LPAREN, "(");
            case ')':
                return make_token(TokenType::TOKEN_RPAREN, ")");
            case ',':
                return make_token(TokenType::TOKEN_COMMA, ",");
            case ';':
                return make_token(TokenType::TOKEN_SEMICOLON, ";");
            default:
                return make_token(TokenType::TOKEN_UNKNOWN, std::string(1, c));
            }
        }

        void Lexer::skip_trivial() {
            while (true) {
                skip_whitespace();

                bool skipped = false;
                skipped = skip_single_line_comment() || skipped;
                skipped = skip_block_comment() || skipped;

                if (!skipped) {
                    break;
                }
            }
        }

        void Lexer::skip_whitespace() {
            while (_pos < _input.size() && std::isspace(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        bool Lexer::skip_single_line_comment() {
            if (peek() == '/' && _pos + 1 < _input.size() && _input[_pos + 1] == '/') {
                // consume "//"
                advance();
                advance();
                // skip until end of line or EOF
                while (peek() != '\n' && peek() != '\0') {
                    advance();
                }
                return true;
            }
            return false;
        }

        bool Lexer::skip_block_comment() {
            if (peek() == '/' && _pos + 1 < _input.size() && _input[_pos + 1] == '*') {
                // consume "/*"
                advance();
                advance();

                int depth = 1;

                while (_pos < _input.size() && depth > 0) {
                    if (peek() == '/' && _pos + 1 < _input.size() && _input[_pos + 1] == '*') {
                        // Nested comment start
                        depth++;
                        advance(); // '/'
                        advance(); // '*'
                    } else if (peek() == '*' && _pos + 1 < _input.size() && _input[_pos + 1] == '/') {
                        // Comment end
                        depth--;
                        advance(); // '*'
                        advance(); // '/'
                    } else {
                        advance();
                    }
                }
                return true;
            }
            return false;
        }

        char Lexer::peek() const {
            return _pos >= _input.size() ? '\0' : _input[_pos];
        }

        char Lexer::advance() {
            if (_pos >= _input.size()) {
                return '\0';
            }
            char c = _input[_pos++];
            if (c == '\n') {
                _line++;
                _column = 1;
            } else {
                // Only increment column if it's not a continuation byte (0x80 - 0xBF)
                // This makes column count correspond to characters (code points) rather than bytes
                if ((c & 0xC0) != 0x80) {
                    _column++;
                }
            }
            return c;
        }

        Token Lexer::make_token(const TokenType &type, const std::string &value) const {
            return Token{type, value, _line, _column - value.size()};
        }
        Token Lexer::make_token(const TokenType &type, const std::string &value, size_t line, size_t column) const {
            return Token{type, value, line, column};
        }

        Token Lexer::scan_identifier() {
            std::string value;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
                value += advance();
            }
            return make_token(TokenType::TOKEN_IDENT, value);
        }

        Token Lexer::scan_string() {
            std::string value;
            int start_line = _line;
            int start_column = _column;
            advance(); // skip opening quote

            while (peek() != '"' && peek() != '\0') {
                char c = advance();
                if (c == '\\') {
                    if (peek() == '"') {
                        value += '"';
                        advance();
                    } else {
                        value += c;
                    }
                } else {
                    value += c;
                }
            }

            if (peek() == '\0') {
                return make_token(TokenType::TOKEN_UNKNOWN, value, start_line, start_column);
            }

            advance(); // skip closing quote
            return make_token(TokenType::TOKEN_STRING, value, start_line, start_column);
        }

    } // namespace config_parser
} // namespace punp
