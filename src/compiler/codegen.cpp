/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Code generator: AST -> IR lowering, register allocation, and emission.
 *
 * Pipeline (see ir.h):
 *   1. Declarations pass: constants, enums, entity types, global addresses,
 *      sprite/table data emitted into a data section after the entry stub.
 *   2. Per function: lower the AST to IR over virtual registers, run the
 *      linear-scan allocator (spilling to memory when the 13 physical
 *      registers run out), then emit bytecode.
 *   3. Resolve call fixups, then the peephole pass cleans up.
 *
 * Only base CHIP-8 opcodes are emitted, using quirk-neutral idioms so the
 * output runs on any standard CHIP-8 emulator.
 */

#include <chip8/compiler/codegen.h>
#include <stdexcept>
#include <string>

namespace chip8 {
    namespace compiler {

        // CHIP-8 memory limits
        static constexpr size_t MAX_ROM_SIZE = 0xDFF - 0x200 + 1;  // 3072 bytes (0x200-0xDFF)
        static constexpr size_t ROM_WARNING_THRESHOLD = MAX_ROM_SIZE * 90 / 100;

        CodeGenerator::CodeGenerator(ErrorHandler& errorHandler)
            : errorHandler(errorHandler) {
        }

        void CodeGenerator::emit_byte(uint8_t byte) {
            output.push_back(byte);
        }

        void CodeGenerator::emit_opcode(uint16_t opcode) {
            emit_byte(static_cast<uint8_t>((opcode >> 8) & 0xFF));
            emit_byte(static_cast<uint8_t>(opcode & 0xFF));
        }

        IRInst& CodeGenerator::emit_ir(IROp op) {
            IRInst& inst = F->emit(op);
            inst.line = current_stmt_line;
            inst.col = current_stmt_col;
            return inst;
        }

        // ------------------------------------------------------------------
        // Constant helpers (AST level)
        // ------------------------------------------------------------------

        std::optional<int> CodeGenerator::try_get_constant(ExprNode* expr) {
            if (auto* num_expr = dynamic_cast<NumberExprNode*>(expr)) {
                return num_expr->value;
            }
            if (auto* str_expr = dynamic_cast<StringExprNode*>(expr)) {
                return str_expr->value.empty() ? 0 : static_cast<int>(static_cast<uint8_t>(str_expr->value[0]));
            }
            if (auto* var_expr = dynamic_cast<VariableExprNode*>(expr)) {
                auto it = constants.find(var_expr->name);
                if (it != constants.end()) {
                    return it->second;
                }
            }
            return std::nullopt;
        }

        std::optional<int> CodeGenerator::eval_binary_op(int left, int right, TokenType op) {
            switch (op) {
                case TokenType::PLUS:      return (left + right) & 0xFF;
                case TokenType::MINUS:     return (left - right) & 0xFF;
                case TokenType::MULTIPLY:  return (left * right) & 0xFF;
                case TokenType::DIVIDE:    if (right == 0) return std::nullopt; return (left / right) & 0xFF;
                case TokenType::AMPERSAND: return left & right;
                case TokenType::PIPE:      return left | right;
                case TokenType::CARET:     return left ^ right;
                case TokenType::MODULO:    if (right == 0) return std::nullopt; return (left % right) & 0xFF;
                case TokenType::SHIFT_LEFT:  return (right >= 8) ? 0 : ((left << right) & 0xFF);
                case TokenType::SHIFT_RIGHT: return (right >= 8) ? 0 : ((left & 0xFF) >> right);
                default: return std::nullopt;
            }
        }

        // Does evaluating `expr` read variable `name`? With skip_left_spine,
        // the leftmost operand chain of binary nodes is excluded: an
        // accumulator-style evaluation writes the target only after reading
        // the left spine, so those reads are safe.
        bool CodeGenerator::expr_reads_var(ExprNode* expr, const std::string& name, bool skip_left_spine) {
            if (!expr) return false;
            if (auto* var = dynamic_cast<VariableExprNode*>(expr)) {
                if (constants.count(var->name)) return false;
                return var->name == name;
            }
            if (auto* bin = dynamic_cast<BinaryExprNode*>(expr)) {
                return expr_reads_var(bin->left.get(), name, skip_left_spine) ||
                       expr_reads_var(bin->right.get(), name, false);
            }
            if (auto* cond = dynamic_cast<ConditionNode*>(expr)) {
                return expr_reads_var(cond->left.get(), name, false) ||
                       expr_reads_var(cond->right.get(), name, false);
            }
            if (auto* log = dynamic_cast<LogicalExprNode*>(expr)) {
                return expr_reads_var(log->left.get(), name, false) ||
                       expr_reads_var(log->right.get(), name, false);
            }
            if (auto* un = dynamic_cast<UnaryExprNode*>(expr))
                return expr_reads_var(un->operand.get(), name, false);
            if (auto* key = dynamic_cast<KeyExprNode*>(expr))
                return expr_reads_var(key->key_num.get(), name, false);
            if (auto* rnd = dynamic_cast<RandExprNode*>(expr))
                return expr_reads_var(rnd->max_val.get(), name, false);
            if (auto* arr = dynamic_cast<ArrayAccessExprNode*>(expr)) {
                for (const auto& idx : arr->indices)
                    if (expr_reads_var(idx.get(), name, false)) return true;
                return false;
            }
            if (auto* call = dynamic_cast<FunctionCallExprNode*>(expr)) {
                for (const auto& arg : call->arguments)
                    if (expr_reads_var(arg.get(), name, false)) return true;
                return false;
            }
            if (auto* ent = dynamic_cast<EntityFieldAccessExpr*>(expr))
                return expr_reads_var(ent->index.get(), name, false);
            if (dynamic_cast<NumberExprNode*>(expr) || dynamic_cast<StringExprNode*>(expr) ||
                dynamic_cast<WaitKeyExprNode*>(expr) || dynamic_cast<CollisionExprNode*>(expr) ||
                dynamic_cast<TimerExprNode*>(expr)) {
                return false;
            }
            return true; // unknown node: be conservative
        }

        // ------------------------------------------------------------------
        // Expression building
        // ------------------------------------------------------------------

        // Move v into hint if a hint was given; otherwise return v as-is.
        VReg CodeGenerator::materialize(VReg v, VReg hint) {
            if (hint == NO_VREG || v == hint) return v;
            IRInst& m = emit_ir(IROp::Move);
            m.dst = hint;
            m.src1 = v;
            return hint;
        }

        // dst = src * c using shift-add decomposition (double-and-add).
        VReg CodeGenerator::mult_by_const(VReg src, int c, VReg hint) {
            c &= 0xFF;
            if (c == 0) {
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = 0;
                return r;
            }
            if (c == 1) return materialize(src, hint);

            VReg acc = (hint != NO_VREG && hint != src) ? hint : fresh();
            int top = 7;
            while (!((c >> top) & 1)) top--;
            { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = src; }
            for (int bit = top - 1; bit >= 0; bit--) {
                { IRInst& s = emit_ir(IROp::Shl); s.dst = acc; }
                if ((c >> bit) & 1) {
                    IRInst& a = emit_ir(IROp::Add); a.dst = acc; a.src1 = src;
                }
            }
            return acc;
        }

        // Runtime (register-register) binary operation. acc is owned by the
        // caller and is mutated in place; returns acc.
        VReg CodeGenerator::build_runtime_binop(VReg acc, VReg right, TokenType op) {
            switch (op) {
                case TokenType::PLUS:  { IRInst& i = emit_ir(IROp::Add); i.dst = acc; i.src1 = right; break; }
                case TokenType::MINUS: { IRInst& i = emit_ir(IROp::Sub); i.dst = acc; i.src1 = right; break; }
                case TokenType::AMPERSAND: { IRInst& i = emit_ir(IROp::And); i.dst = acc; i.src1 = right; break; }
                case TokenType::PIPE:  { IRInst& i = emit_ir(IROp::Or); i.dst = acc; i.src1 = right; break; }
                case TokenType::CARET: { IRInst& i = emit_ir(IROp::Xor); i.dst = acc; i.src1 = right; break; }

                case TokenType::MULTIPLY: {
                    // acc = acc * right via repeated addition
                    VReg mul = fresh();   // running result
                    VReg counter = fresh();
                    { IRInst& i = emit_ir(IROp::Const); i.dst = mul; i.imm = 0; }
                    { IRInst& i = emit_ir(IROp::Move); i.dst = counter; i.src1 = right; }
                    int loop = F->new_label();
                    int end = F->new_label();
                    { IRInst& i = emit_ir(IROp::Label); i.imm = loop; }
                    { IRInst& i = emit_ir(IROp::BrEqI); i.src1 = counter; i.imm2 = 0; i.imm = end; }
                    { IRInst& i = emit_ir(IROp::Add); i.dst = mul; i.src1 = acc; }
                    { IRInst& i = emit_ir(IROp::AddI); i.dst = counter; i.imm = 0xFF; }
                    { IRInst& i = emit_ir(IROp::Jump); i.imm = loop; }
                    { IRInst& i = emit_ir(IROp::Label); i.imm = end; }
                    { IRInst& i = emit_ir(IROp::Move); i.dst = acc; i.src1 = mul; }
                    break;
                }
                case TokenType::DIVIDE:
                case TokenType::MODULO: {
                    // Repeated subtraction. Quotient in counter, remainder in acc.
                    // Note: right == 0 at runtime hangs (constant 0 is a
                    // compile error upstream).
                    VReg counter = fresh();
                    { IRInst& i = emit_ir(IROp::Const); i.dst = counter; i.imm = 0; }
                    int loop = F->new_label();
                    int end = F->new_label();
                    { IRInst& i = emit_ir(IROp::Label); i.imm = loop; }
                    VReg cmp = fresh();
                    { IRInst& i = emit_ir(IROp::Move); i.dst = cmp; i.src1 = acc; }
                    { IRInst& i = emit_ir(IROp::Sub); i.dst = cmp; i.src1 = right; } // VF=1 if acc>=right
                    { IRInst& i = emit_ir(IROp::BrVF); i.imm2 = 0; i.imm = end; }    // acc < right: done
                    { IRInst& i = emit_ir(IROp::Sub); i.dst = acc; i.src1 = right; }
                    { IRInst& i = emit_ir(IROp::AddI); i.dst = counter; i.imm = 1; }
                    { IRInst& i = emit_ir(IROp::Jump); i.imm = loop; }
                    { IRInst& i = emit_ir(IROp::Label); i.imm = end; }
                    if (op == TokenType::DIVIDE) {
                        IRInst& i = emit_ir(IROp::Move); i.dst = acc; i.src1 = counter;
                    }
                    break;
                }
                case TokenType::SHIFT_LEFT:
                case TokenType::SHIFT_RIGHT: {
                    VReg counter = fresh();
                    { IRInst& i = emit_ir(IROp::Move); i.dst = counter; i.src1 = right; }
                    int loop = F->new_label();
                    int end = F->new_label();
                    { IRInst& i = emit_ir(IROp::Label); i.imm = loop; }
                    { IRInst& i = emit_ir(IROp::BrEqI); i.src1 = counter; i.imm2 = 0; i.imm = end; }
                    { IRInst& i = emit_ir(op == TokenType::SHIFT_LEFT ? IROp::Shl : IROp::Shr); i.dst = acc; }
                    { IRInst& i = emit_ir(IROp::AddI); i.dst = counter; i.imm = 0xFF; }
                    { IRInst& i = emit_ir(IROp::Jump); i.imm = loop; }
                    { IRInst& i = emit_ir(IROp::Label); i.imm = end; }
                    break;
                }
                default:
                    errorHandler.error("Invalid binary operator", current_stmt_line, current_stmt_col);
                    break;
            }
            return acc;
        }

        VReg CodeGenerator::build_binary(BinaryExprNode& node, VReg hint) {
            auto left_const = try_get_constant(node.left.get());
            auto right_const = try_get_constant(node.right.get());

            // Full constant folding
            if (left_const && right_const) {
                auto result = eval_binary_op(*left_const, *right_const, node.op);
                if (result) {
                    VReg r = (hint != NO_VREG) ? hint : fresh();
                    IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = *result & 0xFF;
                    return r;
                }
            }

            // Immediate add/sub
            if (right_const && (node.op == TokenType::PLUS || node.op == TokenType::MINUS)) {
                VReg l = build_expr(node.left.get());
                VReg acc = (hint != NO_VREG) ? hint : (l != NO_VREG ? fresh() : fresh());
                if (l != acc) { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = l; }
                int k = (node.op == TokenType::PLUS) ? (*right_const & 0xFF) : ((-*right_const) & 0xFF);
                if (k != 0) { IRInst& a = emit_ir(IROp::AddI); a.dst = acc; a.imm = k; }
                return acc;
            }

            // Strength-reduced multiply/divide/modulo by constant
            if (right_const && (node.op == TokenType::MULTIPLY || node.op == TokenType::DIVIDE ||
                                node.op == TokenType::MODULO)) {
                int c = *right_const & 0xFF;
                if ((node.op == TokenType::DIVIDE || node.op == TokenType::MODULO) && c == 0) {
                    errorHandler.error("Division by zero", node.line, node.column);
                    return (hint != NO_VREG) ? hint : fresh();
                }
                if (node.op == TokenType::MULTIPLY) {
                    VReg l = build_expr(node.left.get());
                    return mult_by_const(l, c, hint);
                }
                if (c == 1) {
                    if (node.op == TokenType::MODULO) {
                        build_expr(node.left.get()); // side effects only
                        VReg r = (hint != NO_VREG) ? hint : fresh();
                        IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = 0;
                        return r;
                    }
                    return build_expr(node.left.get(), hint);
                }
                if ((c & (c - 1)) == 0) { // power of two
                    int shifts = 0;
                    while ((1 << shifts) < c) shifts++;
                    if (node.op == TokenType::DIVIDE) {
                        VReg l = build_expr(node.left.get());
                        VReg acc = (hint != NO_VREG) ? hint : fresh();
                        if (acc != l) { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = l; }
                        for (int i = 0; i < shifts; i++) { IRInst& s = emit_ir(IROp::Shr); s.dst = acc; }
                        return acc;
                    } else { // MODULO: mask
                        VReg l = build_expr(node.left.get());
                        VReg acc = (hint != NO_VREG) ? hint : fresh();
                        if (acc != l) { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = l; }
                        VReg mask = fresh();
                        { IRInst& k = emit_ir(IROp::Const); k.dst = mask; k.imm = (c - 1) & 0xFF; }
                        { IRInst& a = emit_ir(IROp::And); a.dst = acc; a.src1 = mask; }
                        return acc;
                    }
                }
                // Non-power-of-two divide/modulo falls through to runtime loop
            }

            // Constant shift counts unroll
            if (right_const && (node.op == TokenType::SHIFT_LEFT || node.op == TokenType::SHIFT_RIGHT)) {
                int n = *right_const & 0xFF;
                VReg l = build_expr(node.left.get());
                if (n == 0) return materialize(l, hint);
                VReg acc = (hint != NO_VREG) ? hint : fresh();
                if (n >= 8) {
                    IRInst& k = emit_ir(IROp::Const); k.dst = acc; k.imm = 0;
                    return acc;
                }
                if (acc != l) { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = l; }
                for (int i = 0; i < n; i++) {
                    IRInst& s = emit_ir(node.op == TokenType::SHIFT_LEFT ? IROp::Shl : IROp::Shr);
                    s.dst = acc;
                }
                return acc;
            }

            // General case: accumulate into a register the caller may own.
            // hint == l is fine (in-place x += y): the caller's reads-var
            // check guarantees the right side cannot observe the target.
            VReg l = build_expr(node.left.get());
            VReg acc = (hint != NO_VREG) ? hint : fresh();
            if (acc != l) { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = l; }
            VReg r = build_expr(node.right.get());
            return build_runtime_binop(acc, r, node.op);
        }

        // Comparison in value position: produce 0/1
        VReg CodeGenerator::build_condition_value(ConditionNode& node) {
            VReg dst = fresh();
            switch (node.op) {
                case TokenType::EQUALS:
                case TokenType::NOT_EQUALS: {
                    int done = F->new_label();
                    { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = (node.op == TokenType::EQUALS) ? 0 : 1; }
                    VReg l = build_expr(node.left.get());
                    auto rc = try_get_constant(node.right.get());
                    if (rc) {
                        IRInst& b = emit_ir(IROp::BrNeI);
                        b.src1 = l; b.imm2 = *rc & 0xFF; b.imm = done;
                    } else {
                        VReg r = build_expr(node.right.get());
                        IRInst& b = emit_ir(IROp::BrNe);
                        b.src1 = l; b.src2 = r; b.imm = done;
                    }
                    { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = (node.op == TokenType::EQUALS) ? 1 : 0; }
                    { IRInst& lb = emit_ir(IROp::Label); lb.imm = done; }
                    break;
                }
                default: {
                    // Ordered: t = a - b (or b - a), then read VF.
                    //   a <  b: VF == 0 after (a - b)
                    //   a >= b: VF == 1 after (a - b)
                    //   a >  b: VF == 0 after (b - a)
                    //   a <= b: VF == 1 after (b - a)
                    bool swap = (node.op == TokenType::GREATER_THAN || node.op == TokenType::LESS_EQUAL);
                    int want_vf = (node.op == TokenType::GREATER_EQUAL || node.op == TokenType::LESS_EQUAL) ? 1 : 0;

                    VReg l = build_expr(node.left.get());
                    VReg r = build_expr(node.right.get());
                    VReg t = fresh();
                    { IRInst& m = emit_ir(IROp::Move); m.dst = t; m.src1 = swap ? r : l; }
                    { IRInst& s = emit_ir(IROp::Sub); s.dst = t; s.src1 = swap ? l : r; }
                    int done = F->new_label();
                    { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 1; }
                    { IRInst& b = emit_ir(IROp::BrVF); b.imm2 = want_vf; b.imm = done; }
                    { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 0; }
                    { IRInst& lb = emit_ir(IROp::Label); lb.imm = done; }
                    break;
                }
            }
            return dst;
        }

        // Wait: Const dst,1 executes BEFORE the SUB in ordered comparisons
        // above, which would clobber an operand if dst aliased one - but dst
        // is always fresh, so intervals overlap and the allocator separates
        // them.

        VReg CodeGenerator::build_logical_value(LogicalExprNode& node) {
            // Short-circuit, producing 0/1
            VReg dst = fresh();
            int set_short = F->new_label();
            int done = F->new_label();

            if (node.op == TokenType::AND_AND) {
                build_branch(node.left.get(), set_short, false);   // left false -> 0
                VReg r = build_expr(node.right.get());
                { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 0; }
                { IRInst& b = emit_ir(IROp::BrEqI); b.src1 = r; b.imm2 = 0; b.imm = done; }
                { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 1; }
                { IRInst& j = emit_ir(IROp::Jump); j.imm = done; }
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = set_short; }
                { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 0; }
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = done; }
            } else {
                build_branch(node.left.get(), set_short, true);    // left true -> 1
                VReg r = build_expr(node.right.get());
                { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 0; }
                { IRInst& b = emit_ir(IROp::BrEqI); b.src1 = r; b.imm2 = 0; b.imm = done; }
                { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 1; }
                { IRInst& j = emit_ir(IROp::Jump); j.imm = done; }
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = set_short; }
                { IRInst& k = emit_ir(IROp::Const); k.dst = dst; k.imm = 1; }
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = done; }
            }
            return dst;
        }

        VReg CodeGenerator::build_expr(ExprNode* expr, VReg hint) {
            if (!expr) {
                errorHandler.error("Internal: null expression", current_stmt_line, current_stmt_col);
                return (hint != NO_VREG) ? hint : fresh();
            }

            if (auto* num = dynamic_cast<NumberExprNode*>(expr)) {
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = num->value & 0xFF;
                return r;
            }
            if (auto* str = dynamic_cast<StringExprNode*>(expr)) {
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& k = emit_ir(IROp::Const);
                k.dst = r;
                k.imm = str->value.empty() ? 0 : static_cast<uint8_t>(str->value[0]);
                return r;
            }
            if (auto* var = dynamic_cast<VariableExprNode*>(expr)) {
                // Named constant?
                auto cit = constants.find(var->name);
                if (cit != constants.end()) {
                    VReg r = (hint != NO_VREG) ? hint : fresh();
                    IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = cit->second & 0xFF;
                    return r;
                }
                // Global (memory-backed)?
                auto git = arrays.find(var->name);
                if (git != arrays.end() && array_sizes.count(var->name) && array_sizes[var->name] == 1) {
                    VReg r = (hint != NO_VREG) ? hint : fresh();
                    IRInst& l = emit_ir(IROp::LoadG); l.dst = r; l.imm = git->second;
                    return r;
                }
                // Local
                auto vit = var_vregs.find(var->name);
                if (vit == var_vregs.end()) {
                    errorHandler.error("Undefined variable: " + var->name, expr->line, expr->column);
                    return (hint != NO_VREG) ? hint : fresh();
                }
                return materialize(vit->second, hint);
            }
            if (auto* bin = dynamic_cast<BinaryExprNode*>(expr)) {
                return build_binary(*bin, hint);
            }
            if (auto* cond = dynamic_cast<ConditionNode*>(expr)) {
                return materialize(build_condition_value(*cond), hint);
            }
            if (auto* log = dynamic_cast<LogicalExprNode*>(expr)) {
                return materialize(build_logical_value(*log), hint);
            }
            if (auto* un = dynamic_cast<UnaryExprNode*>(expr)) {
                if (un->op == TokenType::NOT) {
                    VReg t = build_expr(un->operand.get());
                    VReg r = (hint != NO_VREG && hint != t) ? hint : fresh();
                    int done = F->new_label();
                    { IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = 1; }
                    { IRInst& b = emit_ir(IROp::BrEqI); b.src1 = t; b.imm2 = 0; b.imm = done; }
                    { IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = 0; }
                    { IRInst& lb = emit_ir(IROp::Label); lb.imm = done; }
                    return r;
                }
                // Unary minus: r = 0 - operand
                VReg t = build_expr(un->operand.get());
                VReg r = (hint != NO_VREG && hint != t) ? hint : fresh();
                { IRInst& k = emit_ir(IROp::Const); k.dst = r; k.imm = 0; }
                { IRInst& s = emit_ir(IROp::Sub); s.dst = r; s.src1 = t; }
                return r;
            }
            if (auto* key = dynamic_cast<KeyExprNode*>(expr)) {
                VReg k = build_expr(key->key_num.get());
                VReg r = (hint != NO_VREG && hint != k) ? hint : fresh();
                IRInst& i = emit_ir(IROp::KeyVal); i.dst = r; i.src1 = k;
                return r;
            }
            if (dynamic_cast<WaitKeyExprNode*>(expr)) {
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& i = emit_ir(IROp::WaitKey); i.dst = r;
                return r;
            }
            if (auto* rnd = dynamic_cast<RandExprNode*>(expr)) {
                uint8_t mask = 0xFF;
                if (auto mc = try_get_constant(rnd->max_val.get())) {
                    mask = static_cast<uint8_t>(*mc & 0xFF);
                }
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& i = emit_ir(IROp::Rand); i.dst = r; i.imm = mask;
                return r;
            }
            if (dynamic_cast<CollisionExprNode*>(expr)) {
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& i = emit_ir(IROp::MoveFromVF); i.dst = r;
                return r;
            }
            if (dynamic_cast<TimerExprNode*>(expr)) {
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& i = emit_ir(IROp::Timer); i.dst = r;
                return r;
            }
            if (auto* arr = dynamic_cast<ArrayAccessExprNode*>(expr)) {
                auto it = arrays.find(arr->array_name);
                if (it == arrays.end()) {
                    // Const ROM table (stored via the sprite machinery)
                    auto rom_it = sprites.find(arr->array_name);
                    if (rom_it != sprites.end()) {
                        if (arr->indices.size() != 1) {
                            errorHandler.error("Const table '" + arr->array_name + "' is one-dimensional",
                                               expr->line, expr->column);
                            return (hint != NO_VREG) ? hint : fresh();
                        }
                        VReg idx = build_expr(arr->indices[0].get());
                        VReg r = (hint != NO_VREG) ? hint : fresh();
                        IRInst& i = emit_ir(IROp::LoadIdx);
                        i.dst = r; i.src1 = idx; i.imm = 0x200 + rom_it->second;
                        return r;
                    }
                    errorHandler.error("Undefined array: " + arr->array_name, expr->line, expr->column);
                    return (hint != NO_VREG) ? hint : fresh();
                }
                VReg idx = build_index(arr->indices, array_dims[arr->array_name]);
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& i = emit_ir(IROp::LoadIdx);
                i.dst = r; i.src1 = idx; i.imm = it->second;
                return r;
            }
            if (auto* call = dynamic_cast<FunctionCallExprNode*>(expr)) {
                IRInst inst{IROp::Call};
                for (const auto& arg : call->arguments) {
                    inst.args.push_back(build_expr(arg.get()));
                }
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& c = emit_ir(IROp::Call);
                c.dst = r;
                c.name = call->function_name;
                c.args = std::move(inst.args);
                return r;
            }
            if (auto* ent = dynamic_cast<EntityFieldAccessExpr*>(expr)) {
                auto inst_it = entity_instances.find(ent->entity_name);
                if (inst_it == entity_instances.end()) {
                    errorHandler.error("Unknown entity variable: " + ent->entity_name, expr->line, expr->column);
                    return (hint != NO_VREG) ? hint : fresh();
                }
                auto type_it = entity_types.find(inst_it->second.type_name);
                int field_offset = -1;
                if (type_it != entity_types.end()) {
                    for (const auto& field : type_it->second.fields) {
                        if (field.name == ent->field_name) { field_offset = field.offset; break; }
                    }
                }
                if (field_offset < 0) {
                    errorHandler.error("Unknown field '" + ent->field_name + "' in entity '" +
                                       inst_it->second.type_name + "'", expr->line, expr->column);
                    return (hint != NO_VREG) ? hint : fresh();
                }
                int esize = type_it->second.size;

                VReg idx;
                if (ent->index) {
                    VReg iv = build_expr(ent->index.get());
                    idx = mult_by_const(iv, esize);
                    if (field_offset > 0) {
                        VReg acc = fresh();
                        { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = idx; }
                        { IRInst& a = emit_ir(IROp::AddI); a.dst = acc; a.imm = field_offset & 0xFF; }
                        idx = acc;
                    }
                } else {
                    idx = fresh();
                    IRInst& k = emit_ir(IROp::Const); k.dst = idx; k.imm = field_offset & 0xFF;
                }
                VReg r = (hint != NO_VREG) ? hint : fresh();
                IRInst& i = emit_ir(IROp::LoadIdx);
                i.dst = r; i.src1 = idx; i.imm = inst_it->second.base_addr;
                return r;
            }

            errorHandler.error("Internal: unhandled expression node", expr->line, expr->column);
            return (hint != NO_VREG) ? hint : fresh();
        }

        // Linear index for (possibly multi-dimensional) array access
        VReg CodeGenerator::build_index(const std::vector<std::unique_ptr<ExprNode>>& indices,
                                        const std::vector<int>& dims) {
            VReg idx = build_expr(indices[0].get());
            if (indices.size() == 1) return idx;

            VReg acc = fresh();
            { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = idx; }
            for (size_t i = 1; i < indices.size() && i < dims.size(); i++) {
                acc = mult_by_const(acc, dims[i]);
                VReg next = build_expr(indices[i].get());
                IRInst& a = emit_ir(IROp::Add); a.dst = acc; a.src1 = next;
            }
            return acc;
        }

        // ------------------------------------------------------------------
        // Branch building (fused conditions)
        // ------------------------------------------------------------------

        void CodeGenerator::build_branch(ExprNode* cond, int target, bool jump_when_true) {
            // !x inverts
            if (auto* un = dynamic_cast<UnaryExprNode*>(cond)) {
                if (un->op == TokenType::NOT) {
                    build_branch(un->operand.get(), target, !jump_when_true);
                    return;
                }
            }

            // Constant condition
            if (auto cval = try_get_constant(cond)) {
                if ((*cval != 0) == jump_when_true) {
                    IRInst& j = emit_ir(IROp::Jump); j.imm = target;
                }
                return;
            }

            // Short-circuit && / ||
            if (auto* log = dynamic_cast<LogicalExprNode*>(cond)) {
                if (log->op == TokenType::AND_AND) {
                    if (!jump_when_true) {
                        build_branch(log->left.get(), target, false);
                        build_branch(log->right.get(), target, false);
                    } else {
                        int after = F->new_label();
                        build_branch(log->left.get(), after, false);
                        build_branch(log->right.get(), target, true);
                        IRInst& lb = emit_ir(IROp::Label); lb.imm = after;
                    }
                } else { // OR
                    if (jump_when_true) {
                        build_branch(log->left.get(), target, true);
                        build_branch(log->right.get(), target, true);
                    } else {
                        int after = F->new_label();
                        build_branch(log->left.get(), after, true);
                        build_branch(log->right.get(), target, false);
                        IRInst& lb = emit_ir(IROp::Label); lb.imm = after;
                    }
                }
                return;
            }

            if (auto* cmp = dynamic_cast<ConditionNode*>(cond)) {
                TokenType op = cmp->op;
                if (!jump_when_true) {
                    // Invert the comparison; we always emit jump-when-true
                    switch (op) {
                        case TokenType::EQUALS:        op = TokenType::NOT_EQUALS; break;
                        case TokenType::NOT_EQUALS:    op = TokenType::EQUALS; break;
                        case TokenType::LESS_THAN:     op = TokenType::GREATER_EQUAL; break;
                        case TokenType::GREATER_EQUAL: op = TokenType::LESS_THAN; break;
                        case TokenType::GREATER_THAN:  op = TokenType::LESS_EQUAL; break;
                        case TokenType::LESS_EQUAL:    op = TokenType::GREATER_THAN; break;
                        default: break;
                    }
                }

                if (op == TokenType::EQUALS || op == TokenType::NOT_EQUALS) {
                    VReg l = build_expr(cmp->left.get());
                    auto rc = try_get_constant(cmp->right.get());
                    if (rc) {
                        IRInst& b = emit_ir(op == TokenType::EQUALS ? IROp::BrEqI : IROp::BrNeI);
                        b.src1 = l; b.imm2 = *rc & 0xFF; b.imm = target;
                    } else {
                        VReg r = build_expr(cmp->right.get());
                        IRInst& b = emit_ir(op == TokenType::EQUALS ? IROp::BrEq : IROp::BrNe);
                        b.src1 = l; b.src2 = r; b.imm = target;
                    }
                } else {
                    bool swap = (op == TokenType::GREATER_THAN || op == TokenType::LESS_EQUAL);
                    int want_vf = (op == TokenType::GREATER_EQUAL || op == TokenType::LESS_EQUAL) ? 1 : 0;
                    VReg l = build_expr(cmp->left.get());
                    VReg r = build_expr(cmp->right.get());
                    VReg t = fresh();
                    { IRInst& m = emit_ir(IROp::Move); m.dst = t; m.src1 = swap ? r : l; }
                    { IRInst& s = emit_ir(IROp::Sub); s.dst = t; s.src1 = swap ? l : r; }
                    { IRInst& b = emit_ir(IROp::BrVF); b.imm2 = want_vf; b.imm = target; }
                }
                return;
            }

            // Generic truthiness
            VReg t = build_expr(cond);
            IRInst& b = emit_ir(jump_when_true ? IROp::BrNeI : IROp::BrEqI);
            b.src1 = t; b.imm2 = 0; b.imm = target;
        }

        // ------------------------------------------------------------------
        // Statement building (visitor methods)
        // ------------------------------------------------------------------

        void CodeGenerator::build_block(BlockNode& block) {
            bool terminated = false;
            for (auto& stmt : block.statements) {
                if (terminated) {
                    errorHandler.warning("Unreachable code after return/break/continue",
                                         stmt->line, stmt->column);
                    break;
                }
                if (stmt->line > 0) {
                    current_stmt_line = stmt->line;
                    current_stmt_col = stmt->column;
                }
                stmt->accept(*this);
                if (dynamic_cast<ReturnNode*>(stmt.get()) ||
                    dynamic_cast<BreakNode*>(stmt.get()) ||
                    dynamic_cast<ContinueNode*>(stmt.get())) {
                    terminated = true;
                }
            }
        }

        void CodeGenerator::visit(BlockNode& node) { build_block(node); }

        void CodeGenerator::visit(VariableDeclNode& node) {
            if (var_vregs.count(node.name)) {
                errorHandler.error("Variable '" + node.name + "' already declared", node.line, node.column);
                return;
            }
            VReg v = fresh();
            var_vregs[node.name] = v;
            if (node.initializer) {
                bool unsafe = expr_reads_var(node.initializer.get(), node.name, true);
                if (!unsafe) {
                    build_expr(node.initializer.get(), v);
                } else {
                    VReg r = build_expr(node.initializer.get());
                    IRInst& m = emit_ir(IROp::Move); m.dst = v; m.src1 = r;
                }
            }
        }

        void CodeGenerator::visit(AssignmentNode& node) {
            // Global?
            auto git = arrays.find(node.name);
            if (git != arrays.end() && array_sizes.count(node.name) && array_sizes[node.name] == 1) {
                VReg r = build_expr(node.value.get());
                IRInst& s = emit_ir(IROp::StoreG); s.src1 = r; s.imm = git->second;
                return;
            }
            auto vit = var_vregs.find(node.name);
            if (vit == var_vregs.end()) {
                errorHandler.error("Undefined variable: " + node.name, node.line, node.column);
                return;
            }
            VReg v = vit->second;
            // Accumulate directly into the variable when the right-hand side
            // cannot observe it (the left spine is evaluated first, so reads
            // there are fine)
            bool unsafe = expr_reads_var(node.value.get(), node.name, true);
            if (!unsafe) {
                build_expr(node.value.get(), v);
            } else {
                VReg r = build_expr(node.value.get());
                if (r != v) { IRInst& m = emit_ir(IROp::Move); m.dst = v; m.src1 = r; }
            }
        }

        void CodeGenerator::visit(ArrayAssignmentNode& node) {
            auto it = arrays.find(node.array_name);
            if (it == arrays.end()) {
                if (sprites.count(node.array_name)) {
                    errorHandler.error("Cannot assign to '" + node.array_name +
                                       "': const tables and sprites are read-only",
                                       node.line, node.column);
                    return;
                }
                errorHandler.error("Undefined array: " + node.array_name, node.line, node.column);
                return;
            }
            VReg val = build_expr(node.value.get());
            VReg idx = build_index(node.indices, array_dims[node.array_name]);
            IRInst& s = emit_ir(IROp::StoreIdx);
            s.src1 = val; s.src2 = idx; s.imm = it->second;
        }

        void CodeGenerator::visit(IfNode& node) {
            int else_label = F->new_label();
            build_branch(node.condition.get(), else_label, false);
            node.then_branch->accept(*this);
            if (node.else_branch) {
                int end_label = F->new_label();
                { IRInst& j = emit_ir(IROp::Jump); j.imm = end_label; }
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = else_label; }
                node.else_branch->accept(*this);
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = end_label; }
            } else {
                IRInst& lb = emit_ir(IROp::Label); lb.imm = else_label;
            }
        }

        void CodeGenerator::visit(WhileNode& node) {
            int start = F->new_label();
            int end = F->new_label();
            { IRInst& lb = emit_ir(IROp::Label); lb.imm = start; }
            build_branch(node.condition.get(), end, false);
            loop_stack.push_back({start, end});
            node.body->accept(*this);
            loop_stack.pop_back();
            { IRInst& j = emit_ir(IROp::Jump); j.imm = start; }
            { IRInst& lb = emit_ir(IROp::Label); lb.imm = end; }
        }

        void CodeGenerator::visit(ForNode& node) {
            if (node.init) node.init->accept(*this);
            int start = F->new_label();
            int increment = F->new_label();
            int end = F->new_label();
            { IRInst& lb = emit_ir(IROp::Label); lb.imm = start; }
            build_branch(node.condition.get(), end, false);
            loop_stack.push_back({increment, end});
            node.body->accept(*this);
            loop_stack.pop_back();
            { IRInst& lb = emit_ir(IROp::Label); lb.imm = increment; }
            if (node.increment) node.increment->accept(*this);
            { IRInst& j = emit_ir(IROp::Jump); j.imm = start; }
            { IRInst& lb = emit_ir(IROp::Label); lb.imm = end; }
        }

        void CodeGenerator::visit(BreakNode& /*node*/) {
            if (loop_stack.empty()) {
                errorHandler.error("break outside of loop", current_stmt_line, current_stmt_col);
                return;
            }
            IRInst& j = emit_ir(IROp::Jump); j.imm = loop_stack.back().break_label;
        }

        void CodeGenerator::visit(ContinueNode& /*node*/) {
            if (loop_stack.empty()) {
                errorHandler.error("continue outside of loop", current_stmt_line, current_stmt_col);
                return;
            }
            IRInst& j = emit_ir(IROp::Jump); j.imm = loop_stack.back().continue_label;
        }

        void CodeGenerator::visit(ReturnNode& node) {
            IRInst ret{IROp::Ret};
            if (node.value) {
                ret.src1 = build_expr(node.value.get());
            }
            IRInst& r = emit_ir(IROp::Ret);
            r.src1 = ret.src1;
        }

        void CodeGenerator::visit(SwitchNode& node) {
            VReg expr = build_expr(node.expr.get());
            int end = F->new_label();
            int default_label = F->new_label();
            bool has_default = false;

            // Dispatch: jump to each case body on match
            std::vector<int> case_labels;
            for (const auto& c : node.cases) {
                if (c.is_default) { has_default = true; case_labels.push_back(default_label); continue; }
                int lbl = F->new_label();
                case_labels.push_back(lbl);
                IRInst& b = emit_ir(IROp::BrEqI);
                b.src1 = expr; b.imm2 = c.value & 0xFF; b.imm = lbl;
            }
            { IRInst& j = emit_ir(IROp::Jump); j.imm = has_default ? default_label : end; }

            // Bodies (each ends with an implicit break, matching previous behavior)
            for (size_t i = 0; i < node.cases.size(); i++) {
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = case_labels[i]; }
                for (const auto& stmt : node.cases[i].statements) {
                    stmt->accept(*this);
                }
                IRInst& j = emit_ir(IROp::Jump); j.imm = end;
            }
            IRInst& lb = emit_ir(IROp::Label); lb.imm = end;
        }

        void CodeGenerator::visit(DrawNode& node) {
            VReg x = build_expr(node.x_expr.get());
            VReg y = build_expr(node.y_expr.get());
            int height = node.height;
            uint16_t addr;
            if (!node.sprite_name.empty()) {
                auto it = sprites.find(node.sprite_name);
                if (it == sprites.end()) {
                    errorHandler.error("Undefined sprite: " + node.sprite_name, node.line, node.column);
                    return;
                }
                addr = 0x200 + it->second;
                if (height == 0) {
                    auto hit = sprite_heights.find(node.sprite_name);
                    height = (hit != sprite_heights.end()) ? hit->second : 5;
                }
            } else {
                addr = static_cast<uint16_t>(node.sprite_id * 5); // built-in font
            }
            IRInst& d = emit_ir(IROp::Draw);
            d.src1 = x; d.src2 = y; d.imm = addr; d.imm2 = height & 0xF;
        }

        void CodeGenerator::visit(ClearNode& /*node*/) {
            emit_ir(IROp::Cls);
        }

        void CodeGenerator::visit(WaitNode& node) {
            // wait(ms): convert to 60Hz timer ticks (~16ms each), minimum 1
            VReg ticks;
            int const_ms = -1;
            if (node.type == WaitNode::WaitType::FIXED) {
                const_ms = node.fixed_duration;
            } else if (auto c = try_get_constant(node.duration.get())) {
                const_ms = *c;
            }
            if (const_ms >= 0) {
                int t = (const_ms / 16) & 0xFF;
                if (t == 0) t = 1;
                ticks = fresh();
                IRInst& k = emit_ir(IROp::Const); k.dst = ticks; k.imm = t;
            } else {
                VReg ms = build_expr(node.duration.get());
                ticks = fresh();
                { IRInst& m = emit_ir(IROp::Move); m.dst = ticks; m.src1 = ms; }
                for (int i = 0; i < 4; i++) { IRInst& s = emit_ir(IROp::Shr); s.dst = ticks; }
                int nonzero = F->new_label();
                { IRInst& b = emit_ir(IROp::BrNeI); b.src1 = ticks; b.imm2 = 0; b.imm = nonzero; }
                { IRInst& a = emit_ir(IROp::AddI); a.dst = ticks; a.imm = 1; }
                { IRInst& lb = emit_ir(IROp::Label); lb.imm = nonzero; }
            }
            IRInst& w = emit_ir(IROp::WaitTicks); w.src1 = ticks;
        }

        void CodeGenerator::visit(BeepNode& node) {
            VReg d = build_expr(node.duration.get());
            IRInst& b = emit_ir(IROp::Beep); b.src1 = d;
        }

        void CodeGenerator::visit(DrawNumNode& node) {
            VReg v = build_expr(node.value.get());
            VReg x = build_expr(node.x_expr.get());
            VReg y = build_expr(node.y_expr.get());
            IRInst& d = emit_ir(IROp::DrawNum);
            d.src1 = v; d.src2 = x; d.src3 = y;
        }

        void CodeGenerator::visit(InlineAsmNode& node) {
            F->raw_pool.push_back(node.opcodes);
            IRInst& r = emit_ir(IROp::Raw);
            r.imm = static_cast<int>(F->raw_pool.size() - 1);
        }

        void CodeGenerator::visit(FunctionCallNode& node) {
            std::vector<VReg> args;
            for (const auto& arg : node.arguments) {
                args.push_back(build_expr(arg.get()));
            }
            IRInst& c = emit_ir(IROp::Call);
            c.name = node.function_name;
            c.args = std::move(args);
        }

        void CodeGenerator::visit(ArrayDeclNode& node) {
            if (arrays.count(node.name)) {
                errorHandler.error("Array '" + node.name + "' already declared", node.line, node.column);
                return;
            }
            arrays[node.name] = next_array_addr;
            array_sizes[node.name] = node.size;
            array_dims[node.name] = node.dimensions;
            next_array_addr += static_cast<uint16_t>(node.size);

            if (next_array_addr > CALLER_SAVE_BASE - 0x10) {
                errorHandler.error("Array allocation exceeds available memory (approaching stack area at 0x" +
                                   std::to_string(CALLER_SAVE_BASE) + ")", node.line, node.column);
            } else if (next_array_addr > CALLER_SAVE_BASE - 0x100) {
                errorHandler.warning("Memory usage high: arrays using " +
                                     std::to_string(next_array_addr - 0x800) + " bytes, " +
                                     std::to_string(CALLER_SAVE_BASE - next_array_addr) + " bytes remaining",
                                     node.line, node.column);
            }
        }

        void CodeGenerator::visit(EntityDeclNode& node) {
            auto type_it = entity_types.find(node.type_name);
            if (type_it == entity_types.end()) {
                errorHandler.error("Unknown entity type: " + node.type_name, node.line, node.column);
                return;
            }
            int entity_size = type_it->second.size;
            int total_size = (node.array_size > 0) ? entity_size * node.array_size : entity_size;

            EntityInstance instance;
            instance.type_name = node.type_name;
            instance.base_addr = next_array_addr;
            instance.array_size = node.array_size;
            entity_instances[node.var_name] = instance;
            next_array_addr += static_cast<uint16_t>(total_size);

            if (next_array_addr > 0xFFF) {
                errorHandler.error("Entity allocation exceeds available memory", node.line, node.column);
            }
        }

        void CodeGenerator::visit(EntityFieldAssignNode& node) {
            auto inst_it = entity_instances.find(node.entity_name);
            if (inst_it == entity_instances.end()) {
                errorHandler.error("Unknown entity variable: " + node.entity_name, node.line, node.column);
                return;
            }
            auto type_it = entity_types.find(inst_it->second.type_name);
            int field_offset = -1;
            if (type_it != entity_types.end()) {
                for (const auto& field : type_it->second.fields) {
                    if (field.name == node.field_name) { field_offset = field.offset; break; }
                }
            }
            if (field_offset < 0) {
                errorHandler.error("Unknown field '" + node.field_name + "' in entity '" +
                                   inst_it->second.type_name + "'", node.line, node.column);
                return;
            }
            int esize = type_it->second.size;

            VReg val = build_expr(node.value.get());
            VReg idx;
            if (node.index) {
                VReg iv = build_expr(node.index.get());
                idx = mult_by_const(iv, esize);
                if (field_offset > 0) {
                    VReg acc = fresh();
                    { IRInst& m = emit_ir(IROp::Move); m.dst = acc; m.src1 = idx; }
                    { IRInst& a = emit_ir(IROp::AddI); a.dst = acc; a.imm = field_offset & 0xFF; }
                    idx = acc;
                }
            } else {
                idx = fresh();
                IRInst& k = emit_ir(IROp::Const); k.dst = idx; k.imm = field_offset & 0xFF;
            }
            IRInst& s = emit_ir(IROp::StoreIdx);
            s.src1 = val; s.src2 = idx; s.imm = inst_it->second.base_addr;
        }

        // Declarations handled in the program pass; harmless if revisited
        void CodeGenerator::visit(ConstDeclNode& node) {
            constants[node.name] = node.value;
        }

        void CodeGenerator::visit(EnumDeclNode& node) {
            for (const auto& ev : node.values) {
                constants[node.name + "." + ev.name] = ev.value;
                if (!constants.count(ev.name)) {
                    constants[ev.name] = ev.value;
                }
            }
        }

        void CodeGenerator::visit(EntityDefNode& node) {
            EntityType type;
            for (const auto& field : node.fields) {
                type.fields.push_back(field);
            }
            type.size = node.size;
            entity_types[node.name] = type;
        }

        void CodeGenerator::visit(GlobalVarDeclNode& node) {
            if (arrays.count(node.name)) return; // registered in the program pass
            arrays[node.name] = next_array_addr;
            array_sizes[node.name] = 1;
            next_array_addr += 1;
        }

        void CodeGenerator::visit(SpriteDefNode& /*node*/) {
            // Sprite/table data is hoisted into the data section during the
            // program pass; nothing to do at statement position.
        }

        // Expression visitor methods (satisfy the interface; the builder
        // dispatches directly, but route through build_expr for safety)
        void CodeGenerator::visit(BinaryExprNode& node) { expr_result = build_binary(node, NO_VREG); }
        void CodeGenerator::visit(VariableExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(NumberExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(StringExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(ConditionNode& node) { expr_result = build_condition_value(node); }
        void CodeGenerator::visit(LogicalExprNode& node) { expr_result = build_logical_value(node); }
        void CodeGenerator::visit(UnaryExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(KeyExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(WaitKeyExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(RandExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(CollisionExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(TimerExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(ArrayAccessExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(FunctionCallExprNode& node) { expr_result = build_expr(&node); }
        void CodeGenerator::visit(EntityFieldAccessExpr& node) { expr_result = build_expr(&node); }

        // ------------------------------------------------------------------
        // Function and program assembly
        // ------------------------------------------------------------------

        void CodeGenerator::build_function_ir(FunctionNode& node) {
            var_vregs.clear();
            loop_stack.clear();

            // Parameters occupy the first vreg ids (EnterFunc defines them)
            for (size_t i = 0; i < node.params.size() && i < 12; i++) {
                VReg v = fresh(); // ids 0..n-1 in an empty function
                var_vregs[node.params[i].name] = v;
            }
            IRInst& enter = emit_ir(IROp::EnterFunc);
            enter.imm = static_cast<int>(node.params.size());

            // main runs global initializers first
            if (node.name == "main") {
                for (GlobalVarDeclNode* g : global_inits) {
                    current_stmt_line = g->line;
                    current_stmt_col = g->column;
                    VReg r = build_expr(g->initializer.get());
                    IRInst& s = emit_ir(IROp::StoreG);
                    s.src1 = r; s.imm = arrays[g->name];
                }
            }

            node.body->accept(*this);

            // Implicit return if the body doesn't end with one
            if (F->insts.empty() || F->insts.back().op != IROp::Ret) {
                emit_ir(IROp::Ret);
            }
        }

        void CodeGenerator::visit(FunctionNode& node) {
            // Handled by generate(); kept for interface completeness
            build_function_ir(node);
        }

        void CodeGenerator::visit(ProgramNode& node) {
            // Handled by generate()
            (void)node;
        }

        void CodeGenerator::emit_sprite_data(SpriteDefNode& node) {
            sprites[node.name] = static_cast<uint16_t>(output.size());
            sprite_heights[node.name] = node.height;
            for (uint8_t b : node.data) emit_byte(b);
            if (output.size() % 2 != 0) emit_byte(0x00);
            data_regions.push_back({sprites[node.name], static_cast<uint16_t>(output.size())});
        }

        // Recursively collect sprite/table definitions (including ones nested
        // in function bodies) into the data section
        void CodeGenerator::collect_sprites(ASTNode* node) {
            if (!node) return;
            if (auto* sprite = dynamic_cast<SpriteDefNode*>(node)) {
                emit_sprite_data(*sprite);
                return;
            }
            if (auto* fn = dynamic_cast<FunctionNode*>(node)) {
                collect_sprites(fn->body.get());
            } else if (auto* block = dynamic_cast<BlockNode*>(node)) {
                for (auto& stmt : block->statements) collect_sprites(stmt.get());
            } else if (auto* if_node = dynamic_cast<IfNode*>(node)) {
                collect_sprites(if_node->then_branch.get());
                collect_sprites(if_node->else_branch.get());
            } else if (auto* while_node = dynamic_cast<WhileNode*>(node)) {
                collect_sprites(while_node->body.get());
            } else if (auto* for_node = dynamic_cast<ForNode*>(node)) {
                collect_sprites(for_node->body.get());
            } else if (auto* switch_node = dynamic_cast<SwitchNode*>(node)) {
                for (auto& c : switch_node->cases) {
                    for (auto& stmt : c.statements) collect_sprites(stmt.get());
                }
            }
        }

        std::vector<uint8_t> CodeGenerator::generate(ProgramNode& program) {
            output.clear();
            labels.clear();
            fixups.clear();
            data_regions.clear();
            source_map.clear();
            function_params.clear();
            function_returns.clear();
            function_max_regs.clear();
            function_spills.clear();
            sprites.clear();
            sprite_heights.clear();
            arrays.clear();
            array_sizes.clear();
            array_dims.clear();
            constants.clear();
            entity_types.clear();
            entity_instances.clear();
            global_inits.clear();
            next_array_addr = 0x800;
            rom_size_warning_issued = false;
            peephole_saved = 0;

            call_graph.clear();
            reachable_functions.clear();
            build_call_graph(program);
            mark_reachable("main");

            try {
                // Entry stub: init the runtime stack pointer, call main, halt
                emit_opcode(0x6D00);                        // LD VD, 0
                fixups.push_back({static_cast<uint16_t>(output.size()), "main", true});
                emit_opcode(0x2000);                        // CALL main (fixup)
                uint16_t halt = static_cast<uint16_t>(output.size());
                emit_opcode(0x1000 | ((0x200 + halt) & 0x0FFF)); // JP self

                // Pass 1: declarations and data
                for (auto& decl : program.functions) {
                    if (auto* c = dynamic_cast<ConstDeclNode*>(decl.get())) {
                        visit(*c);
                    } else if (auto* e = dynamic_cast<EnumDeclNode*>(decl.get())) {
                        visit(*e);
                    } else if (auto* et = dynamic_cast<EntityDefNode*>(decl.get())) {
                        visit(*et);
                    } else if (auto* g = dynamic_cast<GlobalVarDeclNode*>(decl.get())) {
                        if (arrays.count(g->name)) {
                            errorHandler.error("Global variable '" + g->name + "' already declared",
                                               g->line, g->column);
                            continue;
                        }
                        arrays[g->name] = next_array_addr;
                        array_sizes[g->name] = 1;
                        next_array_addr += 1;
                        if (g->initializer) global_inits.push_back(g);
                    }
                }
                // Sprite/table data (top-level and nested) into the data section
                for (auto& decl : program.functions) {
                    collect_sprites(decl.get());
                }

                // Record function signatures up front (forward calls)
                for (auto& decl : program.functions) {
                    if (auto* fn = dynamic_cast<FunctionNode*>(decl.get())) {
                        std::vector<std::string> names;
                        for (const auto& p : fn->params) names.push_back(p.name);
                        function_params[fn->name] = names;
                        function_returns[fn->name] = fn->returns_value;
                    }
                }

                // Pass 2: build, allocate, and emit each reachable function
                for (auto& decl : program.functions) {
                    auto* fn = dynamic_cast<FunctionNode*>(decl.get());
                    if (!fn) continue;
                    if (!reachable_functions.count(fn->name)) continue;

                    IRFunction irf;
                    irf.name = fn->name;
                    irf.param_count = static_cast<int>(fn->params.size());
                    irf.returns_value = fn->returns_value;
                    F = &irf;
                    current_stmt_line = fn->line;
                    current_stmt_col = fn->column;

                    build_function_ir(*fn);

                    // Allocate registers (spill slots live after user arrays)
                    IRAllocator allocator;
                    Allocation alloc;
                    if (!allocator.run(irf, next_array_addr, alloc)) {
                        errorHandler.error("Internal: register allocation failed for " + fn->name,
                                           fn->line, fn->column);
                        F = nullptr;
                        return {};
                    }
                    next_array_addr = std::max(next_array_addr, alloc.spill_end);
                    if (next_array_addr > CALLER_SAVE_BASE - 0x10) {
                        errorHandler.error("Memory exhausted: arrays and spill slots reach the stack area",
                                           fn->line, fn->column);
                    }
                    uint8_t peak = 0;
                    for (auto& [v, p] : alloc.assignment) if (p > peak) peak = p;
                    function_max_regs[fn->name] = peak;
                    function_spills[fn->name] = alloc.spill_count;

                    // Emit
                    labels[fn->name] = static_cast<uint16_t>(output.size());
                    IREmitter emitter;
                    std::vector<IREmitter::CallFixup> call_fixups;
                    std::vector<IREmitter::Mapping> mappings;
                    std::string emit_error;
                    emitter.emit(irf, alloc, output, call_fixups, mappings, emit_error);
                    if (!emit_error.empty()) {
                        errorHandler.error(emit_error, fn->line, fn->column);
                        F = nullptr;
                        return {};
                    }
                    for (const auto& cf : call_fixups) {
                        fixups.push_back({cf.address, cf.name, true});
                    }
                    for (const auto& m : mappings) {
                        source_map.addMapping(static_cast<uint16_t>(0x200 + m.offset), m.line, m.col);
                    }
                    F = nullptr;
                }

                // ROM size checks
                if (output.size() > MAX_ROM_SIZE) {
                    errorHandler.error("ROM size exceeds CHIP-8 limit of 3072 bytes (" +
                                       std::to_string(output.size()) + " bytes)", 0, 0);
                } else if (output.size() > ROM_WARNING_THRESHOLD && !rom_size_warning_issued) {
                    errorHandler.warning("ROM size is at " +
                                         std::to_string(output.size() * 100 / MAX_ROM_SIZE) +
                                         "% capacity (" + std::to_string(output.size()) + "/" +
                                         std::to_string(MAX_ROM_SIZE) + " bytes)", 0, 0);
                    rom_size_warning_issued = true;
                }

                // Peephole (adjusts labels, fixups, data regions, source map)
                peephole_saved = peephole_enabled ? peephole_optimize() : 0;

                // Resolve function-call fixups
                for (const auto& fixup : fixups) {
                    auto it = labels.find(fixup.label);
                    if (it != labels.end()) {
                        uint16_t target_addr = 0x200 + it->second;
                        uint16_t opcode = (fixup.is_call ? 0x2000 : 0x1000) | (target_addr & 0x0FFF);
                        output[fixup.address]     = static_cast<uint8_t>((opcode >> 8) & 0xFF);
                        output[fixup.address + 1] = static_cast<uint8_t>(opcode & 0xFF);
                    } else {
                        errorHandler.error("Undefined function: " + fixup.label, 0, 0);
                    }
                }

                return output;
            } catch (const std::exception& e) {
                errorHandler.error(std::string("Code generation failed: ") + e.what(), 0, 0);
                F = nullptr;
                return {};
            }
        }

        // Dead code elimination: build call graph from AST
        void CodeGenerator::build_call_graph(ProgramNode& program) {
            for (auto& func : program.functions) {
                if (auto* fn = dynamic_cast<FunctionNode*>(func.get())) {
                    call_graph[fn->name] = {};  // Initialize empty set
                    collect_calls_from_node(fn->body.get(), fn->name);
                }
            }
        }

        // Recursively collect function calls from a node
        void CodeGenerator::collect_calls_from_node(ASTNode* node, const std::string& current_func) {
            if (!node) return;

            // Check for function calls
            if (auto* call_stmt = dynamic_cast<FunctionCallNode*>(node)) {
                call_graph[current_func].insert(call_stmt->function_name);
            } else if (auto* call_expr = dynamic_cast<FunctionCallExprNode*>(node)) {
                call_graph[current_func].insert(call_expr->function_name);
            }
            // Recurse into child nodes
            else if (auto* block = dynamic_cast<BlockNode*>(node)) {
                for (auto& stmt : block->statements) {
                    collect_calls_from_node(stmt.get(), current_func);
                }
            } else if (auto* if_node = dynamic_cast<IfNode*>(node)) {
                collect_calls_from_node(if_node->condition.get(), current_func);
                collect_calls_from_node(if_node->then_branch.get(), current_func);
                if (if_node->else_branch) {
                    collect_calls_from_node(if_node->else_branch.get(), current_func);
                }
            } else if (auto* while_node = dynamic_cast<WhileNode*>(node)) {
                collect_calls_from_node(while_node->condition.get(), current_func);
                collect_calls_from_node(while_node->body.get(), current_func);
            } else if (auto* for_node = dynamic_cast<ForNode*>(node)) {
                collect_calls_from_node(for_node->init.get(), current_func);
                collect_calls_from_node(for_node->condition.get(), current_func);
                collect_calls_from_node(for_node->increment.get(), current_func);
                collect_calls_from_node(for_node->body.get(), current_func);
            } else if (auto* switch_node = dynamic_cast<SwitchNode*>(node)) {
                collect_calls_from_node(switch_node->expr.get(), current_func);
                for (auto& c : switch_node->cases) {
                    for (auto& stmt : c.statements) {
                        collect_calls_from_node(stmt.get(), current_func);
                    }
                }
            }
            // Handle expression nodes that might contain function calls
            else if (auto* binary = dynamic_cast<BinaryExprNode*>(node)) {
                collect_calls_from_node(binary->left.get(), current_func);
                collect_calls_from_node(binary->right.get(), current_func);
            } else if (auto* cond = dynamic_cast<ConditionNode*>(node)) {
                collect_calls_from_node(cond->left.get(), current_func);
                collect_calls_from_node(cond->right.get(), current_func);
            } else if (auto* logical = dynamic_cast<LogicalExprNode*>(node)) {
                collect_calls_from_node(logical->left.get(), current_func);
                collect_calls_from_node(logical->right.get(), current_func);
            } else if (auto* unary = dynamic_cast<UnaryExprNode*>(node)) {
                collect_calls_from_node(unary->operand.get(), current_func);
            } else if (auto* assign = dynamic_cast<AssignmentNode*>(node)) {
                collect_calls_from_node(assign->value.get(), current_func);
            } else if (auto* var_decl = dynamic_cast<VariableDeclNode*>(node)) {
                if (var_decl->initializer) {
                    collect_calls_from_node(var_decl->initializer.get(), current_func);
                }
            } else if (auto* ret = dynamic_cast<ReturnNode*>(node)) {
                if (ret->value) {
                    collect_calls_from_node(ret->value.get(), current_func);
                }
            } else if (auto* arr_assign = dynamic_cast<ArrayAssignmentNode*>(node)) {
                for (auto& idx : arr_assign->indices) {
                    collect_calls_from_node(idx.get(), current_func);
                }
                collect_calls_from_node(arr_assign->value.get(), current_func);
            } else if (auto* arr_access = dynamic_cast<ArrayAccessExprNode*>(node)) {
                for (auto& idx : arr_access->indices) {
                    collect_calls_from_node(idx.get(), current_func);
                }
            } else if (auto* draw = dynamic_cast<DrawNode*>(node)) {
                collect_calls_from_node(draw->x_expr.get(), current_func);
                collect_calls_from_node(draw->y_expr.get(), current_func);
            } else if (auto* drawnum = dynamic_cast<DrawNumNode*>(node)) {
                collect_calls_from_node(drawnum->value.get(), current_func);
                collect_calls_from_node(drawnum->x_expr.get(), current_func);
                collect_calls_from_node(drawnum->y_expr.get(), current_func);
            } else if (auto* entity_access = dynamic_cast<EntityFieldAccessExpr*>(node)) {
                if (entity_access->index) {
                    collect_calls_from_node(entity_access->index.get(), current_func);
                }
            } else if (auto* entity_assign = dynamic_cast<EntityFieldAssignNode*>(node)) {
                if (entity_assign->index) {
                    collect_calls_from_node(entity_assign->index.get(), current_func);
                }
                collect_calls_from_node(entity_assign->value.get(), current_func);
            }
        }

        // Mark a function and all functions it calls as reachable
        void CodeGenerator::mark_reachable(const std::string& func_name) {
            if (reachable_functions.count(func_name) > 0) {
                return;  // Already processed
            }
            reachable_functions.insert(func_name);
            
            // Recursively mark all called functions
            auto it = call_graph.find(func_name);
            if (it != call_graph.end()) {
                for (const auto& called : it->second) {
                    mark_reachable(called);
                }
            }
        }

        // Peephole optimization: remove redundant instructions
        size_t CodeGenerator::peephole_optimize() {
            if (output.size() < 4) return 0;
            
            std::vector<bool> remove(output.size(), false);  // Mark bytes to remove
            size_t bytes_saved = 0;
            
            // Returns true if the opcode is a conditional skip (3xkk, 4xkk,
            // 5xy0, 9xy0, Ex9E, ExA1). The instruction after a skip must not
            // be removed or the skip would jump over the wrong instruction.
            auto is_skip = [](uint16_t op) {
                uint16_t hi = op & 0xF000;
                return hi == 0x3000 || hi == 0x4000 || hi == 0x5000 ||
                       hi == 0x9000 || hi == 0xE000;
            };

            auto in_data = [&](size_t off) {
                for (const auto& [start, end] : data_regions) {
                    if (off >= start && off < end) return true;
                }
                return false;
            };

            // Addresses of still-unresolved fixups (function calls, wait-loop
            // jumps): these instructions get patched after this pass and must
            // never be removed
            std::set<uint16_t> fixup_addrs;
            for (const auto& f : fixups) fixup_addrs.insert(f.address);

            // Scan for patterns (instructions are 2 bytes each)
            for (size_t i = 0; i + 3 < output.size(); i += 2) {
                // Never rewrite sprite/table data bytes
                if (in_data(i) || in_data(i + 2)) continue;

                uint16_t op1 = (output[i] << 8) | output[i + 1];
                uint16_t op2 = (output[i + 2] << 8) | output[i + 3];

                // Never remove the instruction that a skip jumps over
                bool op1_follows_skip = (i >= 2) &&
                    is_skip((output[i - 2] << 8) | output[i - 1]);
                bool op2_follows_skip = is_skip(op1);
                
                // Pattern 1: LD Vx, Vx (8xy0 where x == y) - no-op
                if ((op1 & 0xF00F) == 0x8000 && !op1_follows_skip) {
                    uint8_t x = (op1 >> 8) & 0xF;
                    uint8_t y = (op1 >> 4) & 0xF;
                    if (x == y) {
                        remove[i] = remove[i + 1] = true;
                        bytes_saved += 2;
                        continue;
                    }
                }
                
                // Pattern 2: LD Vx, Vy followed by LD Vy, Vx (redundant pair)
                if ((op1 & 0xF00F) == 0x8000 && (op2 & 0xF00F) == 0x8000) {
                    uint8_t x1 = (op1 >> 8) & 0xF;
                    uint8_t y1 = (op1 >> 4) & 0xF;
                    uint8_t x2 = (op2 >> 8) & 0xF;
                    uint8_t y2 = (op2 >> 4) & 0xF;
                    if (x1 == y2 && y1 == x2 && !op2_follows_skip) {
                        // Second LD is redundant after first
                        remove[i + 2] = remove[i + 3] = true;
                        bytes_saved += 2;
                        continue;
                    }
                }
                
                // Pattern 3: ADD Vx, 0 - no-op
                if ((op1 & 0xF0FF) == 0x7000 && !op1_follows_skip) {
                    remove[i] = remove[i + 1] = true;
                    bytes_saved += 2;
                    continue;
                }
                
                // Pattern 4: LD Vx, NN followed by LD Vx, MM - first is dead
                if ((op1 & 0xF000) == 0x6000 && (op2 & 0xF000) == 0x6000 && !op1_follows_skip) {
                    uint8_t x1 = (op1 >> 8) & 0xF;
                    uint8_t x2 = (op2 >> 8) & 0xF;
                    if (x1 == x2) {
                        remove[i] = remove[i + 1] = true;
                        bytes_saved += 2;
                        continue;
                    }
                }

                // Pattern 5: JP to the very next instruction - no-op.
                // (Not when it follows a skip: the skip needs something to
                // skip. Not when a fixup will patch it later.)
                if ((op1 & 0xF000) == 0x1000 && (op1 & 0x0FFF) == 0x200 + i + 2 &&
                    !op1_follows_skip && !fixup_addrs.count(static_cast<uint16_t>(i))) {
                    remove[i] = remove[i + 1] = true;
                    bytes_saved += 2;
                    continue;
                }

                // Pattern 6: conditional skip over a JP to the next
                // instruction - both paths land on the same instruction and
                // skips have no side effects, so the whole pair is dead
                if (is_skip(op1) && (op2 & 0xF000) == 0x1000 &&
                    (op2 & 0x0FFF) == 0x200 + i + 4 &&
                    !op1_follows_skip && !fixup_addrs.count(static_cast<uint16_t>(i + 2))) {
                    remove[i] = remove[i + 1] = true;
                    remove[i + 2] = remove[i + 3] = true;
                    bytes_saved += 4;
                    continue;
                }
            }
            
            // Also check last instruction for LD Vx, Vx
            if (output.size() >= 2) {
                size_t i = output.size() - 2;
                uint16_t op = (output[i] << 8) | output[i + 1];
                bool follows_skip = (i >= 2) && is_skip((output[i - 2] << 8) | output[i - 1]);
                if (follows_skip || in_data(i)) op = 0; // never remove skipped instructions or data
                if ((op & 0xF00F) == 0x8000) {
                    uint8_t x = (op >> 8) & 0xF;
                    uint8_t y = (op >> 4) & 0xF;
                    if (x == y && !remove[i]) {
                        remove[i] = remove[i + 1] = true;
                        bytes_saved += 2;
                    }
                }
                // ADD Vx, 0
                if ((op & 0xF0FF) == 0x7000 && !remove[i]) {
                    remove[i] = remove[i + 1] = true;
                    bytes_saved += 2;
                }
            }
            
            if (bytes_saved == 0) return 0;
            
            // Build removal offset map: for each position, how many bytes were removed before it
            std::vector<size_t> offset_adjust(output.size() + 1, 0);
            size_t removed_so_far = 0;
            for (size_t i = 0; i < output.size(); i++) {
                offset_adjust[i] = removed_so_far;
                if (remove[i]) removed_so_far++;
            }
            offset_adjust[output.size()] = removed_so_far;
            
            // Rebuild output without removed bytes
            std::vector<uint8_t> new_output;
            new_output.reserve(output.size() - bytes_saved);
            for (size_t i = 0; i < output.size(); i++) {
                if (!remove[i]) {
                    new_output.push_back(output[i]);
                }
            }
            
            // Update all labels
            for (auto& [name, offset] : labels) {
                offset -= static_cast<uint16_t>(offset_adjust[offset]);
            }
            
            // Update all fixups
            for (auto& fixup : fixups) {
                fixup.address -= static_cast<uint16_t>(offset_adjust[fixup.address]);
            }

            // Update data region extents (keep originals for the LD I scan below)
            auto original_regions = data_regions;
            for (auto& [start, end] : data_regions) {
                start -= static_cast<uint16_t>(offset_adjust[start]);
                end -= static_cast<uint16_t>(offset_adjust[end]);
            }

            // Update sprite addresses
            for (auto& [name, addr] : sprites) {
                addr -= static_cast<uint16_t>(offset_adjust[addr]);
            }

            // Update source map addresses (mappings use absolute 0x200-based addresses)
            {
                std::vector<SourceMapping> old_mappings = source_map.getAllMappings();
                source_map.clear();
                for (const auto& m : old_mappings) {
                    size_t old_offset = m.address - 0x200;
                    uint16_t new_addr = m.address;
                    if (old_offset < offset_adjust.size()) {
                        new_addr = static_cast<uint16_t>(m.address - offset_adjust[old_offset]);
                    }
                    source_map.addMapping(new_addr, m.line, m.column, m.filename);
                }
            }
            
            // Update all jump (1xxx) and call (2xxx) targets that were already patched
            for (size_t i = 0; i + 1 < new_output.size(); i += 2) {
                // Skip data regions (their extents were adjusted above, so they
                // are in new_output coordinates); data bytes must not be
                // misread as jump instructions and rewritten
                bool in_new_data = false;
                for (const auto& [start, end] : data_regions) {
                    if (i >= start && i < end) { in_new_data = true; break; }
                }
                if (in_new_data) continue;

                uint16_t op = (new_output[i] << 8) | new_output[i + 1];
                uint16_t opcode_type = op & 0xF000;
                
                if (opcode_type == 0x1000 || opcode_type == 0x2000) {
                    // This is a JP or CALL instruction
                    uint16_t old_target = op & 0x0FFF;  // Absolute address
                    if (old_target >= 0x200) {
                        // Convert to byte offset in original output
                        size_t old_offset = old_target - 0x200;
                        if (old_offset < offset_adjust.size()) {
                            // Calculate new target
                            size_t adjustment = offset_adjust[old_offset];
                            uint16_t new_target = static_cast<uint16_t>(old_target - adjustment);
                            uint16_t new_op = (opcode_type) | (new_target & 0x0FFF);
                            new_output[i] = static_cast<uint8_t>((new_op >> 8) & 0xFF);
                            new_output[i + 1] = static_cast<uint8_t>(new_op & 0xFF);
                        }
                    }
                } else if (opcode_type == 0xA000) {
                    // LD I, addr referencing sprite/table data must shift too.
                    // Only adjust targets inside a known data region: other
                    // Axxx targets (arrays at 0x800+, the caller-save area)
                    // are fixed memory addresses unaffected by code removal.
                    uint16_t old_target = op & 0x0FFF;
                    if (old_target >= 0x200) {
                        size_t old_offset = old_target - 0x200;
                        bool is_data = false;
                        for (const auto& [start, end] : original_regions) {
                            if (old_offset >= start && old_offset < end) { is_data = true; break; }
                        }
                        if (is_data && old_offset < offset_adjust.size()) {
                            uint16_t new_target = static_cast<uint16_t>(old_target - offset_adjust[old_offset]);
                            uint16_t new_op = 0xA000 | (new_target & 0x0FFF);
                            new_output[i] = static_cast<uint8_t>((new_op >> 8) & 0xFF);
                            new_output[i + 1] = static_cast<uint8_t>(new_op & 0xFF);
                        }
                    }
                }
            }
            
            output = std::move(new_output);
            return bytes_saved;
        }
    }
}
