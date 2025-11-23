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
            size_t _line = 1;
            size_t _column = 1;

            char peek() const;
            char advance();

            void skip_trivia();
            /**** spec skip type functions ****/
            void skip_whitespace();
            bool skip_single_line_comment();
            bool skip_block_comment();
            /**** spec skip type functions ****/

            Token make_token(const TokenType &type, const std::string &value) const;
            Token make_token(const TokenType &type, const std::string &value, size_t line, size_t column) const;
            Token scan_string();
            Token scan_identifier();
        };

    } // namespace config_parser
} // namespace punp
