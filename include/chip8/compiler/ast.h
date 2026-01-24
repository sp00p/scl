/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Abstract Syntax Tree node definitions for the SCL language.
 */

#pragma once

#include <chip8/compiler/lexer.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

class ASTVisitor;

class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
    int line = 0;
    int column = 0;
};

class ExprNode : public ASTNode {
public:
    virtual ~ExprNode() = default;
};

class ProgramNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> functions;
    void accept(ASTVisitor& visitor) override;
};

struct FunctionParam {
    std::string name;
    int line;
    int column;
};

class FunctionNode : public ASTNode {
public:
    std::string name;
    std::vector<FunctionParam> params;
    bool returns_value = false;
    std::unique_ptr<ASTNode> body;
    void accept(ASTVisitor& visitor) override;
};

class BlockNode : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements;
    void accept(ASTVisitor& visitor) override;
};

class VariableDeclNode : public ASTNode {
public:
    std::string name;
    std::unique_ptr<ExprNode> initializer;
    void accept(ASTVisitor& visitor) override;
};

class AssignmentNode : public ASTNode {
public:
    std::string name;
    std::unique_ptr<ExprNode> value;
    void accept(ASTVisitor& visitor) override;
};

class BinaryExprNode : public ExprNode {
public:
    TokenType op;
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;
    void accept(ASTVisitor& visitor) override;
};

class VariableExprNode : public ExprNode {
public:
    std::string name;
    void accept(ASTVisitor& visitor) override;
};

class NumberExprNode : public ExprNode {
public:
    int value;
    void accept(ASTVisitor& visitor) override;
};

class StringExprNode : public ExprNode {
public:
    std::string value;
    void accept(ASTVisitor& visitor) override;
};

class ConditionNode : public ExprNode {
public:
    TokenType op;
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;
    void accept(ASTVisitor& visitor) override;
};

class LogicalExprNode : public ExprNode {
public:
    TokenType op;
    std::unique_ptr<ExprNode> left;
    std::unique_ptr<ExprNode> right;
    void accept(ASTVisitor& visitor) override;
};

class UnaryExprNode : public ExprNode {
public:
    TokenType op;
    std::unique_ptr<ExprNode> operand;
    void accept(ASTVisitor& visitor) override;
};

class IfNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ASTNode> then_branch;
    std::unique_ptr<ASTNode> else_branch;
    void accept(ASTVisitor& visitor) override;
};

class WhileNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ASTNode> body;
    void accept(ASTVisitor& visitor) override;
};

class ForNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<ExprNode> condition;
    std::unique_ptr<ASTNode> increment;
    std::unique_ptr<ASTNode> body;
    void accept(ASTVisitor& visitor) override;
};

class ReturnNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> value;
    void accept(ASTVisitor& visitor) override;
};

class BreakNode : public ASTNode {
public:
    void accept(ASTVisitor& visitor) override;
};

class ContinueNode : public ASTNode {
public:
    void accept(ASTVisitor& visitor) override;
};

class DrawNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> x_expr;
    std::unique_ptr<ExprNode> y_expr;
    int height;
    int sprite_id;
    std::string sprite_name;
    void accept(ASTVisitor& visitor) override;
};

class ClearNode : public ASTNode {
public:
    void accept(ASTVisitor& visitor) override;
};

class WaitNode : public ASTNode {
public:
    enum class WaitType { FIXED, VARIABLE };
    WaitType type;
    std::unique_ptr<ExprNode> duration;
    int fixed_duration;
    void accept(ASTVisitor& visitor) override;
};

class BeepNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> duration;
    void accept(ASTVisitor& visitor) override;
};

class DrawNumNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> value;
    std::unique_ptr<ExprNode> x_expr;
    std::unique_ptr<ExprNode> y_expr;
    void accept(ASTVisitor& visitor) override;
};

class KeyExprNode : public ExprNode {
public:
    std::unique_ptr<ExprNode> key_num;
    void accept(ASTVisitor& visitor) override;
};

class WaitKeyExprNode : public ExprNode {
public:
    void accept(ASTVisitor& visitor) override;
};

class RandExprNode : public ExprNode {
public:
    std::unique_ptr<ExprNode> max_val;
    void accept(ASTVisitor& visitor) override;
};

class CollisionExprNode : public ExprNode {
public:
    void accept(ASTVisitor& visitor) override;
};

class TimerExprNode : public ExprNode {
public:
    void accept(ASTVisitor& visitor) override;
};

class SpriteDefNode : public ASTNode {
public:
    std::string name;
    int height;
    std::vector<uint8_t> data;
    void accept(ASTVisitor& visitor) override;
};

class ArrayDeclNode : public ASTNode {
public:
    std::string name;
    std::vector<int> dimensions;
    int size;
    void accept(ASTVisitor& visitor) override;
};

class ArrayAccessExprNode : public ExprNode {
public:
    std::string array_name;
    std::vector<std::unique_ptr<ExprNode>> indices;
    void accept(ASTVisitor& visitor) override;
};

class ArrayAssignmentNode : public ASTNode {
public:
    std::string array_name;
    std::vector<std::unique_ptr<ExprNode>> indices;
    std::unique_ptr<ExprNode> value;
    void accept(ASTVisitor& visitor) override;
};

class FunctionCallExprNode : public ExprNode {
public:
    std::string function_name;
    std::vector<std::unique_ptr<ExprNode>> arguments;
    void accept(ASTVisitor& visitor) override;
};

class FunctionCallNode : public ASTNode {
public:
    std::string function_name;
    std::vector<std::unique_ptr<ExprNode>> arguments;
    void accept(ASTVisitor& visitor) override;
};

class ConstDeclNode : public ASTNode {
public:
    std::string name;
    int value;
    void accept(ASTVisitor& visitor) override;
};

class InlineAsmNode : public ASTNode {
public:
    std::vector<uint16_t> opcodes;
    void accept(ASTVisitor& visitor) override;
};

class GlobalVarDeclNode : public ASTNode {
public:
    std::string name;
    std::unique_ptr<ExprNode> initializer;
    void accept(ASTVisitor& visitor) override;
};

struct EnumValue {
    std::string name;
    int value;
};

class EnumDeclNode : public ASTNode {
public:
    std::string name;
    std::vector<EnumValue> values;
    void accept(ASTVisitor& visitor) override;
};

struct SwitchCase {
    int value;
    bool is_default;
    std::vector<std::unique_ptr<ASTNode>> statements;
};

class SwitchNode : public ASTNode {
public:
    std::unique_ptr<ExprNode> expr;
    std::vector<SwitchCase> cases;
    void accept(ASTVisitor& visitor) override;
};

struct EntityField {
    std::string name;
    int offset;
};

class EntityDefNode : public ASTNode {
public:
    std::string name;
    std::vector<EntityField> fields;
    int size;
    void accept(ASTVisitor& visitor) override;
};

class EntityDeclNode : public ASTNode {
public:
    std::string type_name;
    std::string var_name;
    int array_size;
    void accept(ASTVisitor& visitor) override;
};

class EntityFieldAccessExpr : public ExprNode {
public:
    std::string entity_name;
    std::string field_name;
    std::unique_ptr<ExprNode> index;
    void accept(ASTVisitor& visitor) override;
};

class EntityFieldAssignNode : public ASTNode {
public:
    std::string entity_name;
    std::string field_name;
    std::unique_ptr<ExprNode> index;
    std::unique_ptr<ExprNode> value;
    void accept(ASTVisitor& visitor) override;
};

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(ProgramNode& node) = 0;
    virtual void visit(FunctionNode& node) = 0;
    virtual void visit(BlockNode& node) = 0;
    virtual void visit(VariableDeclNode& node) = 0;
    virtual void visit(AssignmentNode& node) = 0;
    virtual void visit(BinaryExprNode& node) = 0;
    virtual void visit(VariableExprNode& node) = 0;
    virtual void visit(NumberExprNode& node) = 0;
    virtual void visit(StringExprNode& node) = 0;
    virtual void visit(ConditionNode& node) = 0;
    virtual void visit(LogicalExprNode& node) = 0;
    virtual void visit(UnaryExprNode& node) = 0;
    virtual void visit(IfNode& node) = 0;
    virtual void visit(WhileNode& node) = 0;
    virtual void visit(ForNode& node) = 0;
    virtual void visit(ReturnNode& node) = 0;
    virtual void visit(BreakNode& node) = 0;
    virtual void visit(ContinueNode& node) = 0;
    virtual void visit(DrawNode& node) = 0;
    virtual void visit(ClearNode& node) = 0;
    virtual void visit(WaitNode& node) = 0;
    virtual void visit(BeepNode& node) = 0;
    virtual void visit(DrawNumNode& node) = 0;
    virtual void visit(KeyExprNode& node) = 0;
    virtual void visit(WaitKeyExprNode& node) = 0;
    virtual void visit(RandExprNode& node) = 0;
    virtual void visit(CollisionExprNode& node) = 0;
    virtual void visit(TimerExprNode& node) = 0;
    virtual void visit(SpriteDefNode& node) = 0;
    virtual void visit(ArrayDeclNode& node) = 0;
    virtual void visit(ArrayAccessExprNode& node) = 0;
    virtual void visit(ArrayAssignmentNode& node) = 0;
    virtual void visit(FunctionCallExprNode& node) = 0;
    virtual void visit(FunctionCallNode& node) = 0;
    virtual void visit(ConstDeclNode& node) = 0;
    virtual void visit(InlineAsmNode& node) = 0;
    virtual void visit(GlobalVarDeclNode& node) = 0;
    virtual void visit(EnumDeclNode& node) = 0;
    virtual void visit(SwitchNode& node) = 0;
    virtual void visit(EntityDefNode& node) = 0;
    virtual void visit(EntityDeclNode& node) = 0;
    virtual void visit(EntityFieldAccessExpr& node) = 0;
    virtual void visit(EntityFieldAssignNode& node) = 0;
};

// Inline accept implementations
inline void ProgramNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void FunctionNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void BlockNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void VariableDeclNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void AssignmentNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void BinaryExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void VariableExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void NumberExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void StringExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ConditionNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void LogicalExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void UnaryExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void IfNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void WhileNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ForNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ReturnNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void BreakNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ContinueNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void DrawNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ClearNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void WaitNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void BeepNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void DrawNumNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void KeyExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void WaitKeyExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void RandExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void CollisionExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void TimerExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void SpriteDefNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ArrayDeclNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ArrayAccessExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ArrayAssignmentNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void FunctionCallExprNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void FunctionCallNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void ConstDeclNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void InlineAsmNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void GlobalVarDeclNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void EnumDeclNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void SwitchNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void EntityDefNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void EntityDeclNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void EntityFieldAccessExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
inline void EntityFieldAssignNode::accept(ASTVisitor& visitor) { visitor.visit(*this); }

}
}
