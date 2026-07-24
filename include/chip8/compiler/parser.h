/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Recursive descent parser for SCL. Converts tokens into an AST.
 */

#pragma once

#include "ast.h"
#include "error_handler.h"
#include <map>
#include <memory>
#include <vector>

namespace chip8 {
namespace compiler {

class Parser {
public:
    Parser(ErrorHandler& errorHandler);
    std::unique_ptr<ProgramNode> parse(const std::vector<Token>& tokens);

private:
    const Token& current();
    const Token& peek();
    bool match(TokenType type);
    void advance();
    bool check(TokenType type);
    void expect(TokenType type, const std::string& message);

    std::unique_ptr<FunctionNode> parse_function();
    std::unique_ptr<BlockNode> parse_block();
    std::unique_ptr<ASTNode> parse_statement();
    std::unique_ptr<ASTNode> parse_variable_declaration();
    std::unique_ptr<ASTNode> parse_assignment_or_call();
    std::unique_ptr<ReturnNode> parse_return_statement();
    std::unique_ptr<FunctionCallNode> parse_function_call(const std::string& name, int line, int col);
    std::unique_ptr<ExprNode> parse_expression();
    std::unique_ptr<ExprNode> parse_bitwise_or();
    std::unique_ptr<ExprNode> parse_bitwise_xor();
    std::unique_ptr<ExprNode> parse_bitwise_and();
    std::unique_ptr<ExprNode> parse_shift();
    std::unique_ptr<ExprNode> parse_additive();
    std::unique_ptr<ExprNode> parse_multiplicative();
    std::unique_ptr<ExprNode> parse_comparison();
    std::unique_ptr<ExprNode> parse_condition();
    std::unique_ptr<IfNode> parse_if_statement();
    std::unique_ptr<WhileNode> parse_while_statement();
    std::unique_ptr<SwitchNode> parse_switch_statement();
    std::unique_ptr<ForNode> parse_for_statement();
    std::unique_ptr<DrawNode> parse_draw_statement();
    std::unique_ptr<WaitNode> parse_wait_statement();
    std::unique_ptr<BeepNode> parse_beep_statement();
    std::unique_ptr<SpriteDefNode> parse_sprite_definition();
    std::unique_ptr<DrawNumNode> parse_drawnum_statement();
    std::unique_ptr<ArrayDeclNode> parse_array_declaration();
    std::unique_ptr<ArrayAssignmentNode> parse_array_assignment();
    std::unique_ptr<ExprNode> parse_primary_expression();
    std::unique_ptr<ASTNode> parse_const_declaration();
    std::unique_ptr<EnumDeclNode> parse_enum_declaration();
    std::unique_ptr<GlobalVarDeclNode> parse_global_var_declaration();
    std::unique_ptr<EntityDefNode> parse_entity_definition();
    std::unique_ptr<EntityDeclNode> parse_entity_declaration(const std::string& type_name);

    void error(const std::string& message);
    bool is_entity_type(const std::string& name);

    const std::vector<Token>* tokens;
    size_t current_token;
    ErrorHandler& errorHandler;
    std::map<std::string, std::vector<EntityField>> entity_types;
};

}
}
