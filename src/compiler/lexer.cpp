/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Lexer implementation
 */

#include <chip8/compiler/lexer.h>
#include <cctype>
#include <stdexcept>

namespace chip8 {
namespace compiler {

const std::map<std::string, TokenType> Lexer::keywords = {
    {"void", TokenType::VOID},
    {"byte", TokenType::BYTE},
    {"if", TokenType::IF},
    {"else", TokenType::ELSE},
    {"while", TokenType::WHILE},
    {"for", TokenType::FOR},
    {"return", TokenType::RETURN},
    {"draw", TokenType::DRAW},
    {"clear", TokenType::CLEAR},
    {"wait", TokenType::WAIT},
    {"key", TokenType::KEY},
    {"waitkey", TokenType::WAITKEY},
    {"sprite", TokenType::SPRITE},
    {"rand", TokenType::RAND},
    {"beep", TokenType::BEEP},
    {"collision", TokenType::COLLISION},
    {"break", TokenType::BREAK},
    {"continue", TokenType::CONTINUE},
    {"timer", TokenType::TIMER},
    {"drawnum", TokenType::DRAWNUM},
    {"const", TokenType::CONST},
    {"asm", TokenType::ASM},
    {"global", TokenType::GLOBAL},
    {"include", TokenType::INCLUDE},
    {"enum", TokenType::ENUM},
    {"switch", TokenType::SWITCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"entity", TokenType::ENTITY},
};

Lexer::Lexer() = default;

std::vector<Token> Lexer::tokenize(const std::string& source) {
    std::vector<Token> tokens;
    int line = 1;
    int col = 1;

    auto push = [&](TokenType t, const std::string& v, int l, int c) {
        tokens.emplace_back(t, v, l, c);
    };

    const char* s = source.c_str();
    size_t i = 0;
    while (i < source.size()) {
        char c = s[i];
        if (c == '\r') { i++; continue; }
        if (c == '\n') { line++; col = 1; i++; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { i++; col++; continue; }

        // Line comments
        if (c == '/' && i + 1 < source.size() && s[i + 1] == '/') {
            i += 2; col += 2;
            while (i < source.size() && s[i] != '\n') { i++; col++; }
            continue;
        }

        // Block comments
        if (c == '/' && i + 1 < source.size() && s[i + 1] == '*') {
            i += 2; col += 2;
            bool closed = false;
            while (i < source.size()) {
                if (s[i] == '\n') { line++; col = 1; i++; continue; }
                if (s[i] == '*' && i + 1 < source.size() && s[i + 1] == '/') {
                    i += 2; col += 2; closed = true; break;
                }
                i++; col++;
            }
            if (!closed) throw std::runtime_error("Unterminated block comment");
            continue;
        }

        // Identifiers / Keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            int start_col = col;
            size_t start = i;
            i++; col++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
                i++; col++;
            }
            std::string word = source.substr(start, i - start);
            auto it = keywords.find(word);
            if (it != keywords.end()) {
                push(it->second, word, line, start_col);
            } else {
                push(TokenType::IDENTIFIER, word, line, start_col);
            }
            continue;
        }

        // Numbers (decimal and hex)
        if (std::isdigit(static_cast<unsigned char>(c))) {
            int start_col = col;
            size_t start = i;
            if (c == '0' && i + 1 < source.size() && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                i += 2; col += 2;
                while (i < source.size() && std::isxdigit(static_cast<unsigned char>(s[i]))) { i++; col++; }
                push(TokenType::HEX_NUMBER, source.substr(start, i - start), line, start_col);
            } else {
                i++; col++;
                while (i < source.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { i++; col++; }
                push(TokenType::NUMBER, source.substr(start, i - start), line, start_col);
            }
            continue;
        }

        // String literals
        if (c == '"') {
            int start_col = col;
            i++; col++;
            std::string str_val;
            while (i < source.size() && s[i] != '"') {
                if (s[i] == '\n') {
                    throw std::runtime_error("Unterminated string literal");
                }
                if (s[i] == '\\' && i + 1 < source.size()) {
                    i++; col++;
                    switch (s[i]) {
                        case 'n': str_val += '\n'; break;
                        case 'r': str_val += '\r'; break;
                        case 't': str_val += '\t'; break;
                        case '\\': str_val += '\\'; break;
                        case '"': str_val += '"'; break;
                        case '0': str_val += '\0'; break;
                        default: str_val += s[i]; break;
                    }
                } else {
                    str_val += s[i];
                }
                i++; col++;
            }
            if (i >= source.size()) {
                throw std::runtime_error("Unterminated string literal");
            }
            i++; col++;
            push(TokenType::STRING, str_val, line, start_col);
            continue;
        }

        // Operators and punctuation
        int tok_line = line, tok_col = col;
        switch (c) {
            case '+':
                if (i + 1 < source.size() && s[i + 1] == '+') {
                    push(TokenType::PLUS_PLUS, "++", tok_line, tok_col); i += 2; col += 2;
                } else if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::PLUS_ASSIGN, "+=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::PLUS, "+", tok_line, tok_col); i++; col++;
                }
                break;
            case '-':
                if (i + 1 < source.size() && s[i + 1] == '-') {
                    push(TokenType::MINUS_MINUS, "--", tok_line, tok_col); i += 2; col += 2;
                } else if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::MINUS_ASSIGN, "-=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::MINUS, "-", tok_line, tok_col); i++; col++;
                }
                break;
            case '*':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::MULTIPLY_ASSIGN, "*=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::MULTIPLY, "*", tok_line, tok_col); i++; col++;
                }
                break;
            case '/':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::DIVIDE_ASSIGN, "/=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::DIVIDE, "/", tok_line, tok_col); i++; col++;
                }
                break;
            case '&':
                if (i + 1 < source.size() && s[i + 1] == '&') {
                    push(TokenType::AND_AND, "&&", tok_line, tok_col); i += 2; col += 2;
                } else if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::AND_ASSIGN, "&=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::AMPERSAND, "&", tok_line, tok_col); i++; col++;
                }
                break;
            case '|':
                if (i + 1 < source.size() && s[i + 1] == '|') {
                    push(TokenType::OR_OR, "||", tok_line, tok_col); i += 2; col += 2;
                } else if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::OR_ASSIGN, "|=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::PIPE, "|", tok_line, tok_col); i++; col++;
                }
                break;
            case '^':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::XOR_ASSIGN, "^=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::CARET, "^", tok_line, tok_col); i++; col++;
                }
                break;
            case '=':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::EQUALS, "==", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::ASSIGN, "=", tok_line, tok_col); i++; col++;
                }
                break;
            case '!':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::NOT_EQUALS, "!=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::NOT, "!", tok_line, tok_col); i++; col++;
                }
                break;
            case '<':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::LESS_EQUAL, "<=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::LESS_THAN, "<", tok_line, tok_col); i++; col++;
                }
                break;
            case '>':
                if (i + 1 < source.size() && s[i + 1] == '=') {
                    push(TokenType::GREATER_EQUAL, ">=", tok_line, tok_col); i += 2; col += 2;
                } else {
                    push(TokenType::GREATER_THAN, ">", tok_line, tok_col); i++; col++;
                }
                break;
            case '[': push(TokenType::LBRACKET, "[", tok_line, tok_col); i++; col++; break;
            case ']': push(TokenType::RBRACKET, "]", tok_line, tok_col); i++; col++; break;
            case ';': push(TokenType::SEMICOLON, ";", tok_line, tok_col); i++; col++; break;
            case ',': push(TokenType::COMMA, ",", tok_line, tok_col); i++; col++; break;
            case '.': push(TokenType::DOT, ".", tok_line, tok_col); i++; col++; break;
            case ':': push(TokenType::COLON, ":", tok_line, tok_col); i++; col++; break;
            case '(': push(TokenType::LPAREN, "(", tok_line, tok_col); i++; col++; break;
            case ')': push(TokenType::RPAREN, ")", tok_line, tok_col); i++; col++; break;
            case '{': push(TokenType::LBRACE, "{", tok_line, tok_col); i++; col++; break;
            case '}': push(TokenType::RBRACE, "}", tok_line, tok_col); i++; col++; break;
            default:
                throw std::runtime_error(std::string("Unexpected character: ") + c);
        }
    }

    tokens.emplace_back(TokenType::END_OF_FILE, "", line, col);
    return tokens;
}

}
}
