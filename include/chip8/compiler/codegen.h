/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Code generator that traverses the AST and emits CHIP-8 bytecode.
 */

#pragma once

#include "ast.h"
#include "error_handler.h"
#include "symbol_table.h"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

struct Variable {
    std::string name;
    uint8_t reg;

    Variable(const std::string& n, uint8_t r) : name(n), reg(r) {}
};

class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator(ErrorHandler& errorHandler);
    std::vector<uint8_t> generate(ProgramNode& program);

    void visit(ProgramNode& node) override;
    void visit(FunctionNode& node) override;
    void visit(BlockNode& node) override;
    void visit(VariableDeclNode& node) override;
    void visit(AssignmentNode& node) override;
    void visit(BinaryExprNode& node) override;
    void visit(VariableExprNode& node) override;
    void visit(NumberExprNode& node) override;
    void visit(StringExprNode& node) override;
    void visit(ConditionNode& node) override;
    void visit(IfNode& node) override;
    void visit(WhileNode& node) override;
    void visit(ForNode& node) override;
    void visit(DrawNode& node) override;
    void visit(ClearNode& node) override;
    void visit(WaitNode& node) override;
    void visit(KeyExprNode& node) override;
    void visit(WaitKeyExprNode& node) override;
    void visit(RandExprNode& node) override;
    void visit(CollisionExprNode& node) override;
    void visit(BeepNode& node) override;
    void visit(SpriteDefNode& node) override;
    void visit(BreakNode& node) override;
    void visit(ContinueNode& node) override;
    void visit(TimerExprNode& node) override;
    void visit(UnaryExprNode& node) override;
    void visit(DrawNumNode& node) override;
    void visit(ArrayDeclNode& node) override;
    void visit(ArrayAccessExprNode& node) override;
    void visit(ArrayAssignmentNode& node) override;
    void visit(ReturnNode& node) override;
    void visit(FunctionCallExprNode& node) override;
    void visit(FunctionCallNode& node) override;
    void visit(ConstDeclNode& node) override;
    void visit(InlineAsmNode& node) override;
    void visit(GlobalVarDeclNode& node) override;
    void visit(EnumDeclNode& node) override;
    void visit(LogicalExprNode& node) override;
    void visit(SwitchNode& node) override;
    void visit(EntityDefNode& node) override;
    void visit(EntityDeclNode& node) override;
    void visit(EntityFieldAccessExpr& node) override;
    void visit(EntityFieldAssignNode& node) override;

private:
    void emit_byte(uint8_t byte);
    void emit_opcode(uint16_t opcode);
    void emit_jump(uint16_t addr_offset_bytes);
    void patch_jump_at(uint16_t at_offset_bytes, uint16_t target_offset_bytes);

    uint8_t allocate_register();
    void free_register(uint8_t reg);
    uint8_t get_variable_register(const std::string& name);
    uint8_t getMaxLocalVariableReg();

    void emit_comparison_node(uint8_t dest_reg, uint8_t left_Reg, uint8_t right_reg, TokenType op);
    void process_binary_operation(uint8_t dest_reg, uint8_t left_reg, uint8_t right_reg, TokenType op);

    std::optional<int> try_get_constant(ExprNode* expr);
    std::optional<int> eval_binary_op(int left, int right, TokenType op);
    void build_call_graph(ProgramNode& program);
    void collect_calls_from_node(ASTNode* node, const std::string& current_func);
    void mark_reachable(const std::string& func_name);
    size_t peephole_optimize();

    std::vector<uint8_t> output;
    std::map<std::string, Variable> variables;
    std::map<std::string, uint16_t> labels;
    std::map<std::string, std::vector<std::string>> function_params;
    std::map<std::string, bool> function_returns;
    std::map<std::string, uint8_t> function_max_regs;
    std::map<std::string, uint16_t> sprites;
    std::map<std::string, int> sprite_heights;
    std::map<std::string, uint16_t> arrays;
    std::map<std::string, int> array_sizes;
    std::map<std::string, std::vector<int>> array_dims;
    std::map<std::string, int> constants;

    struct EntityType {
        std::vector<EntityField> fields;
        int size;
    };
    struct EntityInstance {
        std::string type_name;
        uint16_t base_addr;
        int array_size;
    };
    std::map<std::string, EntityType> entity_types;
    std::map<std::string, EntityInstance> entity_instances;

    uint16_t next_array_addr = 0x800;
    std::vector<bool> used_registers;
    ErrorHandler& errorHandler;
    uint8_t current_result_reg = 1;

    struct Fixup {
        uint16_t address;
        std::string label;
        bool is_call = false;
    };
    std::vector<Fixup> fixups;

    struct LoopContext {
        uint16_t continue_target;
        std::vector<uint16_t> break_fixups;
        std::vector<uint16_t> continue_fixups;
        bool is_for_loop = false;
    };
    std::vector<LoopContext> loop_stack;

    uint16_t call_depth = 0;
    static constexpr uint16_t CALLER_SAVE_BASE = 0xF50;

    std::map<std::string, std::set<std::string>> call_graph;
    std::set<std::string> reachable_functions;
};

}
}
