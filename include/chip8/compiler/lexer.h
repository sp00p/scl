/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Lexical analyzer for the SCL language. Converts source text into tokens.
 */

#pragma once

#include <map>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

enum class TokenType {
    VOID, BYTE, IF, ELSE, WHILE, RETURN, DRAW, CLEAR, WAIT,
    KEY, WAITKEY, SPRITE, RAND, BEEP, COLLISION, FOR,
    BREAK, CONTINUE, TIMER, DRAWNUM,
    CONST, ASM, GLOBAL, ENUM,
    SWITCH, CASE, DEFAULT, COLON,
    ENTITY, DOT,
    INCLUDE, HASH,
    PLUS, MINUS, MULTIPLY, DIVIDE, ASSIGN,
    AMPERSAND, PIPE, CARET,
    PLUS_ASSIGN, MINUS_ASSIGN, MULTIPLY_ASSIGN, DIVIDE_ASSIGN,
    AND_ASSIGN, OR_ASSIGN, XOR_ASSIGN,
    PLUS_PLUS, MINUS_MINUS,
    AND_AND, OR_OR, NOT,
    EQUALS, NOT_EQUALS, LESS_THAN, GREATER_THAN,
    LESS_EQUAL, GREATER_EQUAL,
    SEMICOLON, COMMA, LPAREN, RPAREN, LBRACE, RBRACE,
    LBRACKET, RBRACKET,
    NUMBER, HEX_NUMBER, IDENTIFIER, STRING,
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int column;

    Token(TokenType t, const std::string& v, int l, int c)
        : type(t), value(v), line(l), column(c) {}
};

class Lexer {
public:
    Lexer();
    std::vector<Token> tokenize(const std::string& source);

private:
    static const std::map<std::string, TokenType> keywords;
};

}
}
