/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Code generator: lowers the AST to IR over virtual registers, runs
 * linear-scan register allocation (with memory spilling), and emits
 * CHIP-8 bytecode. See ir.h for the backend pipeline.
 *
 * Portability: only base CHIP-8 opcodes are emitted, using quirk-neutral
 * idioms (shifts with x==y, I always reloaded before memory ops), so
 * compiled ROMs run on any standard CHIP-8 emulator.
 */

#pragma once

#include "ast.h"
#include "error_handler.h"
#include "ir.h"
#include "source_map.h"
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

class CodeGenerator : public ASTVisitor {
public:
    CodeGenerator(ErrorHandler& errorHandler);
    std::vector<uint8_t> generate(ProgramNode& program);
    const SourceMap& getSourceMap() const { return source_map; }
    void setPeepholeEnabled(bool enable) { peephole_enabled = enable; }
    size_t getPeepholeSaved() const { return peephole_saved; }
    const std::map<std::string, uint8_t>& getFunctionRegisterPeaks() const { return function_max_regs; }
    const std::map<std::string, int>& getFunctionSpills() const { return function_spills; }
    size_t getDataBytes() const {
        size_t n = 0;
        for (const auto& r : data_regions) n += r.second - r.first;
        return n;
    }

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
    // --- Emission utilities (final bytecode buffer) ---
    void emit_byte(uint8_t byte);
    void emit_opcode(uint16_t opcode);
    size_t peephole_optimize();

    // --- Dead code elimination ---
    void build_call_graph(ProgramNode& program);
    void collect_calls_from_node(ASTNode* node, const std::string& current_func);
    void mark_reachable(const std::string& func_name);

    // --- AST -> IR building ---
    VReg build_expr(ExprNode* expr, VReg hint = NO_VREG);
    VReg build_binary(BinaryExprNode& node, VReg hint);
    VReg build_condition_value(ConditionNode& node);
    VReg build_logical_value(LogicalExprNode& node);
    void build_branch(ExprNode* cond, int target_label, bool jump_when_true);
    void build_block(BlockNode& block);
    void build_function_ir(FunctionNode& node);
    VReg build_index(const std::vector<std::unique_ptr<ExprNode>>& indices,
                     const std::vector<int>& dims);
    VReg mult_by_const(VReg src, int c, VReg hint = NO_VREG);
    VReg build_runtime_binop(VReg acc, VReg right, TokenType op);
    VReg materialize(VReg v, VReg hint);

    std::optional<int> try_get_constant(ExprNode* expr);
    std::optional<int> eval_binary_op(int left, int right, TokenType op);
    bool expr_reads_var(ExprNode* expr, const std::string& name, bool skip_left_spine);

    void collect_sprites(ASTNode* node);
    void emit_sprite_data(SpriteDefNode& node);

    IRInst& emit_ir(IROp op);
    VReg fresh() { return F->new_vreg(); }

    // --- Output state ---
    std::vector<uint8_t> output;
    std::map<std::string, uint16_t> labels;   // function name -> byte offset
    struct Fixup {
        uint16_t address;
        std::string label;
        bool is_call = false;
    };
    std::vector<Fixup> fixups;
    std::vector<std::pair<uint16_t, uint16_t>> data_regions;
    SourceMap source_map;
    bool peephole_enabled = true;
    size_t peephole_saved = 0;
    bool rom_size_warning_issued = false;

    // --- Symbols ---
    std::map<std::string, std::vector<std::string>> function_params;
    std::map<std::string, bool> function_returns;
    std::map<std::string, uint8_t> function_max_regs; // highest phys used
    std::map<std::string, int> function_spills;
    std::map<std::string, uint16_t> sprites;          // name -> output offset
    std::map<std::string, int> sprite_heights;
    std::map<std::string, uint16_t> arrays;           // name -> absolute address
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
    static constexpr uint16_t CALLER_SAVE_BASE = 0xF50;

    std::map<std::string, std::set<std::string>> call_graph;
    std::set<std::string> reachable_functions;

    // --- Builder state (per function) ---
    IRFunction* F = nullptr;
    std::map<std::string, VReg> var_vregs;
    struct LoopLabels {
        int continue_label;
        int break_label;
    };
    std::vector<LoopLabels> loop_stack;
    VReg expr_result = NO_VREG;
    int current_stmt_line = 0;
    int current_stmt_col = 0;
    std::vector<GlobalVarDeclNode*> global_inits; // initializers run at main entry

    ErrorHandler& errorHandler;
};

}
}
