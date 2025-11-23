#pragma once

#include "config_parser/lexer.h"
#include "config_parser/token.h"
#include "types.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace punp {
    namespace config_parser {

        class Parser {
        public:
            explicit Parser(const std::string &file_path, const std::string &input,
                            std::shared_ptr<ReplacementMap> rep_map_ptr,
                            std::shared_ptr<ProtectedRegions> protected_regions_ptr)
                : _file_path(file_path), _lexer(input),
                  _rep_map_ptr(rep_map_ptr), _protected_regions_ptr(protected_regions_ptr) {
                advance();
                advance();
            };
            ~Parser() = default;

            void parse();
            std::shared_ptr<ReplacementMap> get_replacement_map() const { return _rep_map_ptr; }

        private:
            std::string _file_path;
            Lexer _lexer;
            Token _current_token;
            Token _peek_token;
            std::shared_ptr<ReplacementMap> _rep_map_ptr;
            std::shared_ptr<ProtectedRegions> _protected_regions_ptr;

            /*****  Parsing methods *****/
            void parse_statement();

            bool parse_replace();
            bool parse_del();
            bool parse_protect();
            bool parse_clear();
            /*****  Parsing methods *****/

            // Map: KEYWORD -> parse_function
            using parse_func_t = bool (Parser::*)();
            using parse_func_map_t = std::unordered_map<std::string, parse_func_t>;
            const parse_func_map_t _parse_func_map = {
                {"REPLACE", &Parser::parse_replace},
                {"DEL", &Parser::parse_del},
                {"PROTECT", &Parser::parse_protect},
                {"CLEAR", &Parser::parse_clear},
            };

            void advance();
            bool expect(const TokenType &type);

            void to_upper(std::string &str) const;
            text_t to_tstr(const std::string &str) const;

            // helper method to parse args kv pairs
            using kwargs_keys_t = std::vector<std::string>;
            using kwargs_t = std::unordered_map<std::string, std::string>;
            kwargs_t parse_args(const kwargs_keys_t &kwargs_keys, bool &is_valid);
        };

    } // namespace config_parser
} // namespace punp
