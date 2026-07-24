#include <gtest/gtest.h>
#include <chip8/compiler/lexer.h>

using namespace chip8::compiler;

class LexerTest : public ::testing::Test {
protected:
    Lexer lexer;
};

// Basic token tests
TEST_F(LexerTest, TokenizesKeywords) {
    auto tokens = lexer.tokenize("void byte if else while for");
    
    ASSERT_GE(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, TokenType::VOID);
    EXPECT_EQ(tokens[1].type, TokenType::BYTE);
    EXPECT_EQ(tokens[2].type, TokenType::IF);
    EXPECT_EQ(tokens[3].type, TokenType::ELSE);
    EXPECT_EQ(tokens[4].type, TokenType::WHILE);
    EXPECT_EQ(tokens[5].type, TokenType::FOR);
}

TEST_F(LexerTest, TokenizesNewKeywords) {
    auto tokens = lexer.tokenize("key waitkey sprite rand beep collision");
    
    ASSERT_GE(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, TokenType::KEY);
    EXPECT_EQ(tokens[1].type, TokenType::WAITKEY);
    EXPECT_EQ(tokens[2].type, TokenType::SPRITE);
    EXPECT_EQ(tokens[3].type, TokenType::RAND);
    EXPECT_EQ(tokens[4].type, TokenType::BEEP);
    EXPECT_EQ(tokens[5].type, TokenType::COLLISION);
}

TEST_F(LexerTest, TokenizesBuiltinStatements) {
    auto tokens = lexer.tokenize("draw clear wait");
    
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::DRAW);
    EXPECT_EQ(tokens[1].type, TokenType::CLEAR);
    EXPECT_EQ(tokens[2].type, TokenType::WAIT);
}

TEST_F(LexerTest, TokenizesNumbers) {
    auto tokens = lexer.tokenize("42 0 255");
    
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[0].value, "42");
    EXPECT_EQ(tokens[1].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[1].value, "0");
    EXPECT_EQ(tokens[2].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[2].value, "255");
}

TEST_F(LexerTest, TokenizesHexNumbers) {
    auto tokens = lexer.tokenize("0xFF 0x00 0xAB");
    
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::HEX_NUMBER);
    EXPECT_EQ(tokens[0].value, "0xFF"); // Stored with 0x prefix
    EXPECT_EQ(tokens[1].type, TokenType::HEX_NUMBER);
    EXPECT_EQ(tokens[1].value, "0x00");
    EXPECT_EQ(tokens[2].type, TokenType::HEX_NUMBER);
    EXPECT_EQ(tokens[2].value, "0xAB");
}

TEST_F(LexerTest, TokenizesIdentifiers) {
    auto tokens = lexer.tokenize("myVar _test var123");
    
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[0].value, "myVar");
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "_test");
    EXPECT_EQ(tokens[2].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].value, "var123");
}

TEST_F(LexerTest, TokenizesOperators) {
    auto tokens = lexer.tokenize("+ - * / =");
    
    ASSERT_GE(tokens.size(), 5);
    EXPECT_EQ(tokens[0].type, TokenType::PLUS);
    EXPECT_EQ(tokens[1].type, TokenType::MINUS);
    EXPECT_EQ(tokens[2].type, TokenType::MULTIPLY);
    EXPECT_EQ(tokens[3].type, TokenType::DIVIDE);
    EXPECT_EQ(tokens[4].type, TokenType::ASSIGN);
}

TEST_F(LexerTest, TokenizesBitwiseOperators) {
    auto tokens = lexer.tokenize("& | ^");
    
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[0].type, TokenType::AMPERSAND);
    EXPECT_EQ(tokens[1].type, TokenType::PIPE);
    EXPECT_EQ(tokens[2].type, TokenType::CARET);
}

TEST_F(LexerTest, TokenizesComparisonOperators) {
    auto tokens = lexer.tokenize("== != < > <= >=");
    
    ASSERT_GE(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, TokenType::EQUALS);
    EXPECT_EQ(tokens[1].type, TokenType::NOT_EQUALS);
    EXPECT_EQ(tokens[2].type, TokenType::LESS_THAN);
    EXPECT_EQ(tokens[3].type, TokenType::GREATER_THAN);
    EXPECT_EQ(tokens[4].type, TokenType::LESS_EQUAL);
    EXPECT_EQ(tokens[5].type, TokenType::GREATER_EQUAL);
}

TEST_F(LexerTest, TokenizesPunctuation) {
    auto tokens = lexer.tokenize("; , ( ) { } [ ]");
    
    ASSERT_GE(tokens.size(), 8);
    EXPECT_EQ(tokens[0].type, TokenType::SEMICOLON);
    EXPECT_EQ(tokens[1].type, TokenType::COMMA);
    EXPECT_EQ(tokens[2].type, TokenType::LPAREN);
    EXPECT_EQ(tokens[3].type, TokenType::RPAREN);
    EXPECT_EQ(tokens[4].type, TokenType::LBRACE);
    EXPECT_EQ(tokens[5].type, TokenType::RBRACE);
    EXPECT_EQ(tokens[6].type, TokenType::LBRACKET);
    EXPECT_EQ(tokens[7].type, TokenType::RBRACKET);
}

TEST_F(LexerTest, IgnoresComments) {
    auto tokens = lexer.tokenize("byte x; // this is a comment\nbyte y;");
    
    ASSERT_GE(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, TokenType::BYTE);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[2].type, TokenType::SEMICOLON);
    EXPECT_EQ(tokens[3].type, TokenType::BYTE);
    EXPECT_EQ(tokens[4].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[5].type, TokenType::SEMICOLON);
}

TEST_F(LexerTest, TracksLineNumbers) {
    auto tokens = lexer.tokenize("byte\nx\n;");
    
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[0].line, 1);
    EXPECT_EQ(tokens[1].line, 2);
    EXPECT_EQ(tokens[2].line, 3);
}

TEST_F(LexerTest, TokenizesCompleteFunction) {
    auto tokens = lexer.tokenize("void main() { byte x; x = 5; }");
    
    ASSERT_GE(tokens.size(), 12);
    EXPECT_EQ(tokens[0].type, TokenType::VOID);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "main");
    EXPECT_EQ(tokens[2].type, TokenType::LPAREN);
    EXPECT_EQ(tokens[3].type, TokenType::RPAREN);
    EXPECT_EQ(tokens[4].type, TokenType::LBRACE);
}

TEST_F(LexerTest, TokenizesSpriteDefinition) {
    auto tokens = lexer.tokenize("sprite ball[2] = { 0xC0, 0xC0 };");
    
    ASSERT_GE(tokens.size(), 11);
    EXPECT_EQ(tokens[0].type, TokenType::SPRITE);
    EXPECT_EQ(tokens[1].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[1].value, "ball");
    EXPECT_EQ(tokens[2].type, TokenType::LBRACKET);
    EXPECT_EQ(tokens[3].type, TokenType::NUMBER);
    EXPECT_EQ(tokens[4].type, TokenType::RBRACKET);
    EXPECT_EQ(tokens[5].type, TokenType::ASSIGN);
    EXPECT_EQ(tokens[6].type, TokenType::LBRACE);
    EXPECT_EQ(tokens[7].type, TokenType::HEX_NUMBER);
}

TEST_F(LexerTest, EndsWithEOF) {
    auto tokens = lexer.tokenize("byte x;");
    
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens.back().type, TokenType::END_OF_FILE);
}

// New operator tokens: %, <<, >>
TEST_F(LexerTest, TokenizesModulo) {
    auto tokens = lexer.tokenize("a % b");
    ASSERT_GE(tokens.size(), 3);
    EXPECT_EQ(tokens[1].type, TokenType::MODULO);
}

TEST_F(LexerTest, TokenizesShiftOperators) {
    auto tokens = lexer.tokenize("a << 2 >> 1");
    ASSERT_GE(tokens.size(), 5);
    EXPECT_EQ(tokens[1].type, TokenType::SHIFT_LEFT);
    EXPECT_EQ(tokens[3].type, TokenType::SHIFT_RIGHT);
}

TEST_F(LexerTest, ShiftDoesNotBreakComparisons) {
    auto tokens = lexer.tokenize("a < b <= c > d >= e");
    ASSERT_GE(tokens.size(), 9);
    EXPECT_EQ(tokens[1].type, TokenType::LESS_THAN);
    EXPECT_EQ(tokens[3].type, TokenType::LESS_EQUAL);
    EXPECT_EQ(tokens[5].type, TokenType::GREATER_THAN);
    EXPECT_EQ(tokens[7].type, TokenType::GREATER_EQUAL);
}
