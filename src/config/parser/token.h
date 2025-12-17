#pragma once

#include <string>

namespace punp {
    namespace config_parser {

        enum class TokenType {
            TOKEN_EOF,
            TOKEN_IDENT,
            TOKEN_STRING,
            TOKEN_LPAREN,
            TOKEN_RPAREN,
            TOKEN_COMMA,
            TOKEN_SEMICOLON,
            TOKEN_UNKNOWN,
        };

        struct Token {
            TokenType type;
            std::string value;
            size_t line;
            size_t column;
        };

    } // namespace config_parser
} // namespace punp
