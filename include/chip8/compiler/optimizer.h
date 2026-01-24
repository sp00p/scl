/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * AST optimizer with constant folding and dead code elimination.
 */

#pragma once

#include <chip8/compiler/ast.h>
#include <memory>
#include <vector>

namespace chip8 {
namespace compiler {

class Optimizer {
public:
    Optimizer();

    void setConstantFolding(bool enable) { fold_constants = enable; }
    void setDeadCodeElimination(bool enable) { eliminate_dead_code = enable; }
    void optimize(ProgramNode& program);
    int getConstantsFolded() const { return constants_folded; }
    int getDeadCodeRemoved() const { return dead_code_removed; }

private:
    std::unique_ptr<ExprNode> foldExpression(std::unique_ptr<ExprNode> expr);
    int evaluateConstant(const ExprNode* expr, bool& is_constant);
    void eliminateDeadCode(BlockNode& block);
    bool isDeadCode(const ASTNode* stmt, bool after_return);

    bool fold_constants = true;
    bool eliminate_dead_code = true;
    int constants_folded = 0;
    int dead_code_removed = 0;
};

}
}
