#pragma once

#include "config_parser/common.h"

namespace punp {
    namespace config_parser {

        class Lexer {
        public:
            explicit Lexer(const std::string &input) : _input(input) {};
            ~Lexer() = default;
            Token next_token();

        private:
            std::string _input;
            size_t _pos = 0;
            int _line = 1;
            int _column = 1;

            char peek() const;
            char advance();
            void skip_whitespace_and_comments();
            Token make_token(const TokenType &type, const std::string &value) const;
            Token make_token(const TokenType &type, const std::string &value, int line, int column) const;
            Token scan_string();
            Token scan_identifier();
        };

    } // namespace config_parser
} // namespace punp
