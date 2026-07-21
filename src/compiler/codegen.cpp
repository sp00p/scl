#include <chip8/compiler/codegen.h>
#include <stdexcept>


    namespace chip8::compiler {
        CodeGenerator::CodeGenerator(ErrorHandler &errorHandler)
            : errorHandler(errorHandler) {
            used_registers.resize(16, false);
            used_registers[0] = true; // V0 reserved for system operations
            used_registers[15] = true; // VF reserved for flags
        }

        std::vector<uint8_t> CodeGenerator::generate(ProgramNode &program) {
            output.clear();
            variables.clear();
            labels.clear();
            function_params.clear();
            function_returns.clear();
            sprites.clear();
            sprite_heights.clear();
            arrays.clear();
            array_sizes.clear();
            next_array_addr = 0x800;  // Arrays/globals start at 0x800, leaving room for code
            fixups.clear();
            source_map.clear();
            current_result_reg = 1;
            
            // Dead code elimination: build call graph and find reachable functions
            call_graph.clear();
            reachable_functions.clear();
            build_call_graph(program);
            mark_reachable("main");

            used_registers.assign(16, false);
            used_registers[0] = true;
            used_registers[15] = true;
            call_depth = 0;

            try {
                program.accept(*this);
                
                // Peephole optimization pass (before resolving fixups)
                peephole_optimize();

                // Resolve fixups (JP/CALL targets)
                for (const auto& fixup : fixups) {
                    auto it = labels.find(fixup.label);
                    if (it != labels.end()) {
                        uint16_t target_offset = it->second;               // byte offset in output
                        uint16_t target_addr = 0x200 + target_offset;      // absolute address in memory
                        uint16_t opcode = (fixup.is_call ? 0x2000 : 0x1000) | (target_addr & 0x0FFF);
                        output[fixup.address]     = static_cast<uint8_t>((opcode >> 8) & 0xFF);
                        output[fixup.address + 1] = static_cast<uint8_t>(opcode & 0xFF);
                    } else {
                        errorHandler.error("Undefined label: " + fixup.label, 0, 0);
                    }
                }

                return output;
            } catch (const std::exception& e) {
                errorHandler.error(std::string("Code generation failed: ") + e.what(), 0, 0);
                return {};
            }
        }

        void CodeGenerator::visit(ProgramNode& node) {
            // Initialize VD = 0 (runtime call stack offset)
            emit_opcode(0x6D00); // LD VD, 0

            // Program entry stub: CALL main; JP $ (halt)
            uint16_t call_main_at = output.size();
            emit_opcode(0x2000); // placeholder CALL 0x000
            fixups.push_back({call_main_at, std::string("main"), true});

            // Halt: loop to itself after main returns
            uint16_t halt_offset = output.size();
            emit_jump(halt_offset);

            // Process each function in the program (skip unreachable ones for DCE)
            for (auto& func : node.functions) {
                // Only emit reachable functions
                if (auto* fn = dynamic_cast<FunctionNode*>(func.get())) {
                    if (reachable_functions.count(fn->name) > 0) {
                        func->accept(*this);
                    }
                } else {
                    // Non-function declarations (sprites, constants, etc.) - always emit
                    func->accept(*this);
                }
            }
        }

        void CodeGenerator::visit(FunctionNode& node) {
            // Record function address (byte offset) in the labels map
            labels[node.name] = static_cast<uint16_t>(output.size());

            // Store function signature for call validation
            std::vector<std::string> param_names;
            for (const auto& param : node.params) {
                param_names.push_back(param.name);
            }
            function_params[node.name] = param_names;
            function_returns[node.name] = node.returns_value;

            // Save current variable state (for local scope)
            auto saved_variables = variables;
            auto saved_used_registers = used_registers;

            // Reset register allocation for this function (keep V0 and VF reserved)
            used_registers.assign(16, false);
            used_registers[0] = true;
            used_registers[15] = true;
            variables.clear();

            // Allocate registers for parameters
            // Parameters are passed in V1, V2, V3, ... Vn
            for (size_t i = 0; i < node.params.size() && i < 14; i++) {
                uint8_t reg = static_cast<uint8_t>(i + 1); // V1, V2, etc.
                used_registers[reg] = true;
                variables.emplace(node.params[i].name, Variable(node.params[i].name, reg));
            }

            // Visit the function body
            node.body->accept(*this);
            
            // Record max register used by this function (for smarter caller-save)
            function_max_regs[node.name] = getMaxLocalVariableReg();

            // Add a return instruction at the end of the function
            emit_opcode(0x00EE); // RET

            // Restore variable state (for global scope)
            variables = saved_variables;
            used_registers = saved_used_registers;
        }

        void CodeGenerator::visit(BlockNode& node) {
            bool block_terminated = false;  // Track if we've hit return/break/continue
            
            // Process each statement in the block
            for (size_t i = 0; i < node.statements.size(); i++) {
                auto& stmt = node.statements[i];
                
                // Warn about unreachable code after control flow termination
                if (block_terminated) {
                    errorHandler.warning("Unreachable code after return/break/continue",
                                        stmt->line, stmt->column);
                    break;  // Don't process unreachable statements
                }

                // Record source mapping: this statement's code starts here
                if (stmt->line > 0) {
                    source_map.addMapping(static_cast<uint16_t>(0x200 + output.size()),
                                          stmt->line, stmt->column);
                }

                stmt->accept(*this);
                
                // Check if this statement terminates control flow
                if (dynamic_cast<ReturnNode*>(stmt.get()) ||
                    dynamic_cast<BreakNode*>(stmt.get()) ||
                    dynamic_cast<ContinueNode*>(stmt.get())) {
                    block_terminated = true;
                }
            }
        }

        void CodeGenerator::visit(VariableDeclNode& node) {
            // Skip if variable is already declared
            if (variables.find(node.name) != variables.end()) {
                errorHandler.error("Variable '" + node.name + "' already declared", node.line, node.column);
                return;
            }

            // Allocate a register for the variable
            uint8_t reg = allocate_register();
            if (reg == 0) {
                errorHandler.error("Failed to allocate register for variable '" + node.name + "'", node.line, node.column);
                return;
            }

            // Store variable in the variables map
            variables.emplace(node.name, Variable(node.name, reg));

            // Handle initializer if present: byte x = 10;
            if (node.initializer) {
                uint8_t saved = current_result_reg;
                current_result_reg = reg;
                node.initializer->accept(*this);
                current_result_reg = saved;
            }
        }

        void CodeGenerator::visit(AssignmentNode& node) {
            // Check if this is a global variable (stored in memory)
            auto global_it = arrays.find(node.name);
            if (global_it != arrays.end() && array_sizes.count(node.name) && array_sizes[node.name] == 1) {
                // Global variable - evaluate value into V0 and store to memory
                uint8_t saved = current_result_reg;
                current_result_reg = 0;
                node.value->accept(*this);
                current_result_reg = saved;
                
                emit_opcode(0xA000 | (global_it->second & 0x0FFF)); // LD I, addr
                emit_opcode(0xF055 | (0 << 8)); // LD [I], V0
                return;
            }
            
            // Local variable - destination register
            uint8_t dest_reg = get_variable_register(node.name);

            // Evaluate the expression directly into dest_reg
            uint8_t saved = current_result_reg;
            current_result_reg = dest_reg;
            node.value->accept(*this);
            current_result_reg = saved;
        }

        // Helper to try getting a constant value from an expression
        std::optional<int> CodeGenerator::try_get_constant(ExprNode* expr) {
            if (auto* num_expr = dynamic_cast<NumberExprNode*>(expr)) {
                return num_expr->value;
            }
            if (auto* var_expr = dynamic_cast<VariableExprNode*>(expr)) {
                auto it = constants.find(var_expr->name);
                if (it != constants.end()) {
                    return it->second;
                }
            }
            return std::nullopt;
        }

        // Compile-time evaluation of binary operations for constant folding
        std::optional<int> CodeGenerator::eval_binary_op(int left, int right, TokenType op) {
            switch (op) {
                case TokenType::PLUS:      return (left + right) & 0xFF;
                case TokenType::MINUS:     return (left - right) & 0xFF;
                case TokenType::MULTIPLY:  return (left * right) & 0xFF;
                case TokenType::DIVIDE:    if (right == 0) return std::nullopt; return (left / right) & 0xFF;
                case TokenType::AMPERSAND: return left & right;
                case TokenType::PIPE:      return left | right;
                case TokenType::CARET:     return left ^ right;
                default: return std::nullopt;
            }
        }

        void CodeGenerator::visit(BinaryExprNode& node) {
            // Optimization 1: Constant folding - evaluate at compile time if both operands are constants
            auto left_const = try_get_constant(node.left.get());
            auto right_const = try_get_constant(node.right.get());
            
            if (left_const && right_const) {
                auto result = eval_binary_op(*left_const, *right_const, node.op);
                if (result) {
                    emit_opcode(0x6000 | (current_result_reg << 8) | (*result & 0xFF)); // LD Vx, result
                    return;
                }
            }

            // Optimization 2: Immediate ADD/SUB when right operand is constant
            if (right_const && (node.op == TokenType::PLUS || node.op == TokenType::MINUS)) {
                // Evaluate left operand directly into the destination register,
                // then add the immediate (no temp register needed)
                node.left->accept(*this);

                if (node.op == TokenType::PLUS) {
                    // ADD Vx, byte (7xkk)
                    emit_opcode(0x7000 | (current_result_reg << 8) | (*right_const & 0xFF));
                } else {
                    // SUB via ADD with two's complement: ADD Vx, -byte
                    emit_opcode(0x7000 | (current_result_reg << 8) | ((-*right_const) & 0xFF));
                }
                return;
            }

            // Optimization 3: Strength-reduce multiply/divide by a constant
            // power of two into shifts (avoids the runtime multiply/divide
            // loops and their temp registers entirely)
            if (right_const && (node.op == TokenType::MULTIPLY || node.op == TokenType::DIVIDE)) {
                int c = *right_const & 0xFF;
                if (node.op == TokenType::DIVIDE && c == 0) {
                    errorHandler.error("Division by zero", node.line, node.column);
                    return;
                }
                if (c == 1) {
                    node.left->accept(*this); // x * 1 == x / 1 == x
                    return;
                }
                if (node.op == TokenType::MULTIPLY && c == 0) {
                    emit_opcode(0x6000 | (current_result_reg << 8)); // LD dest, 0
                    return;
                }
                if ((c & (c - 1)) == 0) { // power of two
                    node.left->accept(*this); // evaluate left into dest
                    int shifts = 0;
                    while ((1 << shifts) < c) shifts++;
                    for (int s = 0; s < shifts; s++) {
                        // Shift dest in place (x==y makes both shift-quirk
                        // interpretations equivalent); VF gets the shifted-out bit
                        if (node.op == TokenType::MULTIPLY) {
                            emit_opcode(0x8000 | (current_result_reg << 8) | (current_result_reg << 4) | 0xE); // SHL
                        } else {
                            emit_opcode(0x8000 | (current_result_reg << 8) | (current_result_reg << 4) | 0x6); // SHR
                        }
                    }
                    return;
                }
            }

            // Fallback: Evaluate both operands into temporary registers
            uint8_t left_reg = allocate_register();
            uint8_t right_reg = allocate_register();

            // Evaluate left operand (use visitor for proper global/constant handling)
            uint8_t saved = current_result_reg;
            current_result_reg = left_reg;
            node.left->accept(*this);
            current_result_reg = saved;

            // Evaluate right operand
            saved = current_result_reg;
            current_result_reg = right_reg;
            node.right->accept(*this);
            current_result_reg = saved;

            // Compute into current_result_reg
            process_binary_operation(current_result_reg, left_reg, right_reg, node.op);

            free_register(left_reg);
            free_register(right_reg);
        }

        void CodeGenerator::visit(VariableExprNode& node) {
            // Check if this is a constant first
            auto const_it = constants.find(node.name);
            if (const_it != constants.end()) {
                // Load constant value directly
                emit_opcode(0x6000 | (current_result_reg << 8) | (const_it->second & 0xFF)); // LD Vx, byte
                return;
            }
            
            // Check if this is a global variable (stored in memory, size=1)
            auto global_it = arrays.find(node.name);
            if (global_it != arrays.end() && array_sizes.count(node.name) && array_sizes[node.name] == 1) {
                // Global variable - load from memory into V0, then copy to current_result_reg
                emit_opcode(0xA000 | (global_it->second & 0x0FFF)); // LD I, addr
                emit_opcode(0xF065 | (0 << 8)); // LD V0, [I]
                if (current_result_reg != 0) {
                    emit_opcode(0x8000 | (current_result_reg << 8) | (0 << 4) | 0x0); // LD Vx, V0
                }
                return;
            }
            
            // Local variable - load from register
            uint8_t var_reg = get_variable_register(node.name);
            emit_opcode(0x8000 | (current_result_reg << 8) | (var_reg << 4) | 0x0); // LD Vx, Vy
        }

        void CodeGenerator::visit(NumberExprNode& node) {
            // Load the immediate value into the current_result_reg
            emit_opcode(0x6000 | (current_result_reg << 8) | (node.value & 0xFF)); // LD Vx, byte
        }

        void CodeGenerator::visit(StringExprNode& node) {
            // For string literals, load the ASCII value of the first character
            // This is useful for character comparisons like: if (c == "A") {...}
            // For multi-character strings in arrays, they should be handled during initialization
            if (node.value.empty()) {
                emit_opcode(0x6000 | (current_result_reg << 8) | 0); // LD Vx, 0
            } else {
                uint8_t ascii_val = static_cast<uint8_t>(node.value[0]);
                emit_opcode(0x6000 | (current_result_reg << 8) | ascii_val); // LD Vx, ASCII
            }
        }

        void CodeGenerator::visit(ConditionNode& node) {
            // EQ/NE against a constant can use the immediate skip forms
            // (SE/SNE Vx, kk) - no temp register needed for the right side
            auto right_const = try_get_constant(node.right.get());
            if (right_const && (node.op == TokenType::EQUALS || node.op == TokenType::NOT_EQUALS)) {
                bool left_allocated;
                uint8_t left_reg = get_comparison_operand(node.left.get(), left_allocated);

                emit_opcode(0x6000 | (current_result_reg << 8) | 0x00); // LD dest, 0
                if (node.op == TokenType::EQUALS) {
                    emit_opcode(0x4000 | (left_reg << 8) | (*right_const & 0xFF)); // SNE left, kk
                } else {
                    emit_opcode(0x3000 | (left_reg << 8) | (*right_const & 0xFF)); // SE left, kk
                }
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x01); // LD dest, 1

                if (left_allocated) free_register(left_reg);
                return;
            }

            // Comparison emitters never modify the operand registers, so plain
            // local variables can be compared in place; only complex operands
            // need a temporary
            bool left_allocated, right_allocated;
            uint8_t left_reg = get_comparison_operand(node.left.get(), left_allocated);
            uint8_t right_reg = get_comparison_operand(node.right.get(), right_allocated);

            // Write boolean result into current_result_reg (0 or 1)
            emit_comparison_node(current_result_reg, left_reg, right_reg, node.op);

            if (left_allocated) free_register(left_reg);
            if (right_allocated) free_register(right_reg);
        }

        // Returns a register holding the operand's value. Reuses a local
        // variable's own register when safe (it must not alias the result
        // register, which comparison emitters use as scratch); otherwise
        // allocates a temporary and sets allocated = true.
        uint8_t CodeGenerator::get_comparison_operand(ExprNode* expr, bool& allocated) {
            allocated = false;
            if (auto* var = dynamic_cast<VariableExprNode*>(expr)) {
                // Not a named constant and not a memory-backed global
                if (!constants.count(var->name) && !arrays.count(var->name)) {
                    auto it = variables.find(var->name);
                    if (it != variables.end() && it->second.reg != current_result_reg) {
                        return it->second.reg;
                    }
                }
            }

            allocated = true;
            uint8_t reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = reg;
            expr->accept(*this);
            current_result_reg = saved;
            return reg;
        }

        void CodeGenerator::visit(IfNode& node) {
            // Evaluate condition into a temp register as boolean 0/1
            uint8_t cond_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = cond_reg;
            node.condition->accept(*this);
            current_result_reg = saved;

            // If cond != 0, skip the jump to else
            emit_opcode(0x4000 | (cond_reg << 8) | 0x00); // SNE Vx, 0

            uint16_t jump_to_else_at = output.size();
            emit_jump(0); // placeholder

            // The condition value is dead after the branch above - free its
            // register now so the branch bodies can reuse it
            free_register(cond_reg);

            // then branch
            node.then_branch->accept(*this);

            uint16_t jump_to_end_at = 0;
            if (node.else_branch) {
                jump_to_end_at = output.size();
                emit_jump(0); // placeholder to jump over else
            }

            // patch jump_to_else to current location (start of else)
            patch_jump_at(jump_to_else_at, static_cast<uint16_t>(output.size()));

            // else branch (if any)
            if (node.else_branch) {
                node.else_branch->accept(*this);
                patch_jump_at(jump_to_end_at, static_cast<uint16_t>(output.size()));
            }
        }

        void CodeGenerator::visit(WhileNode& node) {
            uint16_t loop_start = static_cast<uint16_t>(output.size());

            // Push loop context for break/continue
            loop_stack.push_back({loop_start, {}, {}, false});

            // Evaluate condition into a temp register as boolean 0/1
            uint8_t cond_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = cond_reg;
            node.condition->accept(*this);
            current_result_reg = saved;

            // If cond != 0, skip the jump to end (continue loop)
            emit_opcode(0x4000 | (cond_reg << 8) | 0x00); // SNE Vx, 0 => skip next if cond != 0 (true)
            uint16_t jump_to_end_at = output.size();
            emit_jump(0); // placeholder - jump to loop end if cond == 0 (false)

            // Condition value is dead after the branch - free it for the body
            free_register(cond_reg);

            // body
            node.body->accept(*this);

            // jump back to start
            emit_jump(loop_start);

            // patch end target
            uint16_t loop_end = static_cast<uint16_t>(output.size());
            patch_jump_at(jump_to_end_at, loop_end);

            // Patch all break statements
            for (uint16_t break_addr : loop_stack.back().break_fixups) {
                patch_jump_at(break_addr, loop_end);
            }
            loop_stack.pop_back();
        }

        void CodeGenerator::visit(ForNode& node) {
            // Execute init statement
            if (node.init) {
                node.init->accept(*this);
            }

            uint16_t loop_start = static_cast<uint16_t>(output.size());

            // Evaluate condition into a temp register
            uint8_t cond_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = cond_reg;
            node.condition->accept(*this);
            current_result_reg = saved;

            // If cond != 0, skip the jump to end (continue loop)
            emit_opcode(0x4000 | (cond_reg << 8) | 0x00); // SNE Vx, 0 => skip next if cond != 0 (true)
            uint16_t jump_to_end_at = output.size();
            emit_jump(0); // placeholder - jump to loop end if cond == 0 (false)

            // Condition value is dead after the branch - free it for the body
            free_register(cond_reg);

            // For continue in for-loops, we need to jump to the increment, not the condition
            // We use fixups because we don't know the increment address until after the body

            // Push loop context - mark as for-loop so continue uses fixups
            loop_stack.push_back({0, {}, {}, true}); // is_for_loop = true

            // body
            node.body->accept(*this);

            // Record increment start and patch all continue statements
            uint16_t increment_start = static_cast<uint16_t>(output.size());
            for (uint16_t continue_addr : loop_stack.back().continue_fixups) {
                patch_jump_at(continue_addr, increment_start);
            }

            // increment
            if (node.increment) {
                node.increment->accept(*this);
            }

            // jump back to start (condition check)
            emit_jump(loop_start);

            // patch end target
            uint16_t loop_end = static_cast<uint16_t>(output.size());
            patch_jump_at(jump_to_end_at, loop_end);

            // Patch all break statements
            for (uint16_t break_addr : loop_stack.back().break_fixups) {
                patch_jump_at(break_addr, loop_end);
            }
            loop_stack.pop_back();
        }

        void CodeGenerator::visit(DrawNode& node) {
            // Evaluate x coordinate expression into a register
            uint8_t x_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = x_reg;
            node.x_expr->accept(*this);
            current_result_reg = saved;

            // Evaluate y coordinate expression into a register
            uint8_t y_reg = allocate_register();
            saved = current_result_reg;
            current_result_reg = y_reg;
            node.y_expr->accept(*this);
            current_result_reg = saved;

            int height = node.height;

            if (!node.sprite_name.empty()) {
                // Custom sprite - look up address in sprites map
                auto it = sprites.find(node.sprite_name);
                if (it == sprites.end()) {
                    errorHandler.error("Undefined sprite: " + node.sprite_name, node.line, node.column);
                    return;
                }
                uint16_t sprite_addr = 0x200 + it->second; // Absolute address
                emit_opcode(0xA000 | (sprite_addr & 0x0FFF)); // LD I, addr

                // If height was 0, infer from sprite definition
                if (height == 0) {
                    auto height_it = sprite_heights.find(node.sprite_name);
                    if (height_it != sprite_heights.end()) {
                        height = height_it->second;
                    } else {
                        height = 5; // Default fallback
                    }
                }
            } else {
                // Built-in font sprite (0x000-0x050, 5 bytes each)
                uint16_t char_addr = static_cast<uint16_t>(node.sprite_id * 5);
                emit_opcode(0xA000 | (char_addr & 0x0FFF)); // LD I, addr
            }

            // Draw the sprite
            emit_opcode(0xD000 | (x_reg << 8) | (y_reg << 4) | (height & 0xF)); // DRW Vx, Vy, nibble

            // Free temporary registers
            free_register(x_reg);
            free_register(y_reg);
        }

        void CodeGenerator::visit(ClearNode& /*node*/) {
            emit_opcode(0x00E0); // CLS
        }

        void CodeGenerator::visit(WaitNode& node) {
            uint8_t delay_reg = 0; // V0

            if (node.type == WaitNode::WaitType::FIXED) {
                uint8_t ticks = static_cast<uint8_t>((node.fixed_duration / 16) & 0xFF); // approx 60Hz
                if (ticks == 0) ticks = 1; // Minimum 1 tick
                emit_opcode(0x6000 | (delay_reg << 8) | ticks); // LD V0, ticks
            } else if (node.type == WaitNode::WaitType::VARIABLE) {
                if (auto* num_expr = dynamic_cast<NumberExprNode*>(node.duration.get())) {
                    uint8_t ticks = static_cast<uint8_t>((num_expr->value / 16) & 0xFF);
                    if (ticks == 0) ticks = 1;
                    emit_opcode(0x6000 | (delay_reg << 8) | ticks); // LD V0, ticks
                } else {
                    // Evaluate expression into V0, then convert ms -> 60Hz ticks
                    // (divide by 16 via shifts) with a minimum of 1 tick, matching
                    // the constant case above
                    uint8_t saved = current_result_reg;
                    current_result_reg = delay_reg;
                    node.duration->accept(*this);
                    current_result_reg = saved;
                    for (int s = 0; s < 4; s++) {
                        emit_opcode(0x8000 | (delay_reg << 8) | (delay_reg << 4) | 0x6); // SHR V0
                    }
                    emit_opcode(0x4000 | (delay_reg << 8) | 0x00); // SNE V0, 0 -> skip if nonzero
                    emit_opcode(0x7000 | (delay_reg << 8) | 0x01); // ADD V0, 1
                }
            }

            // Set the delay timer
            emit_opcode(0xF015); // LD DT, V0

            // Wait for the timer to reach 0
            // Use a unique label for this wait loop to survive peephole optimization
            static int wait_counter = 0;
            std::string wait_label = "__wait_" + std::to_string(wait_counter++);
            labels[wait_label] = static_cast<uint16_t>(output.size());
            
            emit_opcode(0xF007); // LD V0, DT
            emit_opcode(0x3000 | (delay_reg << 8) | 0x00); // SE V0, 0 -> skip jump if zero (exit)
            
            // Use fixup so peephole optimization doesn't break the jump target
            uint16_t jump_at = static_cast<uint16_t>(output.size());
            emit_opcode(0x1000); // placeholder JP
            fixups.push_back({jump_at, wait_label, false});
        }

        void CodeGenerator::visit(KeyExprNode& node) {
            // Evaluate key number into a temp register
            uint8_t key_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = key_reg;
            node.key_num->accept(*this);
            current_result_reg = saved;

            // Set result to 1 (assume pressed)
            emit_opcode(0x6000 | (current_result_reg << 8) | 0x01); // LD dest, 1
            // Skip next if key is pressed (Ex9E)
            emit_opcode(0xE09E | (key_reg << 8)); // SKP Vx
            // Key not pressed, set result to 0
            emit_opcode(0x6000 | (current_result_reg << 8) | 0x00); // LD dest, 0

            free_register(key_reg);
        }

        void CodeGenerator::visit(WaitKeyExprNode& /*node*/) {
            // Fx0A - wait for key press, store key in Vx
            emit_opcode(0xF00A | (current_result_reg << 8)); // LD Vx, K
        }

        void CodeGenerator::visit(RandExprNode& node) {
            // Evaluate max value
            uint8_t mask = 0xFF;
            if (auto* num_expr = dynamic_cast<NumberExprNode*>(node.max_val.get())) {
                mask = static_cast<uint8_t>(num_expr->value & 0xFF);
            }
            // Cxnn - Vx = random byte AND nn
            emit_opcode(0xC000 | (current_result_reg << 8) | mask);
        }

        void CodeGenerator::visit(CollisionExprNode& /*node*/) {
            // Copy VF to current_result_reg
            emit_opcode(0x8000 | (current_result_reg << 8) | (0xF << 4) | 0x0); // LD Vx, VF
        }

        void CodeGenerator::visit(BeepNode& node) {
            uint8_t sound_reg = 0; // V0

            // Evaluate duration into V0
            if (auto* num_expr = dynamic_cast<NumberExprNode*>(node.duration.get())) {
                emit_opcode(0x6000 | (sound_reg << 8) | (num_expr->value & 0xFF)); // LD V0, byte
            } else if (auto* var_expr = dynamic_cast<VariableExprNode*>(node.duration.get())) {
                uint8_t var_reg = get_variable_register(var_expr->name);
                emit_opcode(0x8000 | (sound_reg << 8) | (var_reg << 4) | 0x0); // LD V0, Vx
            } else {
                uint8_t saved = current_result_reg;
                current_result_reg = sound_reg;
                node.duration->accept(*this);
                current_result_reg = saved;
            }

            // Fx18 - set sound timer
            emit_opcode(0xF018 | (sound_reg << 8)); // LD ST, V0
        }

        void CodeGenerator::visit(SpriteDefNode& node) {
            // Record sprite address and height
            sprites[node.name] = static_cast<uint16_t>(output.size());
            sprite_heights[node.name] = node.height;

            // Emit sprite data bytes
            for (uint8_t byte : node.data) {
                emit_byte(byte);
            }

            // Pad to even address (CHIP-8 requires 2-byte aligned instructions)
            if (output.size() % 2 != 0) {
                emit_byte(0x00);
            }
        }

        void CodeGenerator::visit(BreakNode& /*node*/) {
            if (loop_stack.empty()) {
                errorHandler.error("break outside of loop", 0, 0);
                return;
            }
            // Emit jump placeholder, will be patched when loop ends
            uint16_t break_addr = static_cast<uint16_t>(output.size());
            emit_jump(0); // placeholder
            loop_stack.back().break_fixups.push_back(break_addr);
        }

        void CodeGenerator::visit(ContinueNode& /*node*/) {
            if (loop_stack.empty()) {
                errorHandler.error("continue outside of loop", 0, 0);
                return;
            }
            if (loop_stack.back().is_for_loop) {
                // For loops: use fixup because we don't know increment address yet
                uint16_t continue_addr = static_cast<uint16_t>(output.size());
                emit_jump(0); // placeholder
                loop_stack.back().continue_fixups.push_back(continue_addr);
            } else {
                // While loops: jump directly to continue target (loop start)
                emit_jump(loop_stack.back().continue_target);
            }
        }

        void CodeGenerator::visit(TimerExprNode& /*node*/) {
            // Fx07 - read delay timer into current_result_reg
            emit_opcode(0xF007 | (current_result_reg << 8)); // LD Vx, DT
        }

        void CodeGenerator::visit(UnaryExprNode& node) {
            // Evaluate operand into current_result_reg
            node.operand->accept(*this);

            if (node.op == TokenType::NOT) {
                // Logical NOT: if operand == 0, result = 1, else result = 0
                // Use a temp register to hold the result
                uint8_t temp_reg = allocate_register();
                emit_opcode(0x6000 | (temp_reg << 8) | 0x01);            // LD temp, 1 (assume zero)
                emit_opcode(0x3000 | (current_result_reg << 8) | 0x00); // SE Vx, 0 -> skip if zero
                emit_opcode(0x6000 | (temp_reg << 8) | 0x00);            // LD temp, 0 (was non-zero)
                emit_opcode(0x8000 | (current_result_reg << 8) | (temp_reg << 4) | 0x0); // LD Vx, temp
                free_register(temp_reg);
            }
        }

        void CodeGenerator::visit(DrawNumNode& node) {
            // BCD display: uses Fx33 to convert value to BCD at I, I+1, I+2
            // Then draws three digits

            // Evaluate value into a temp register first
            uint8_t val_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = val_reg;
            node.value->accept(*this);
            current_result_reg = saved;

            // Evaluate x coordinate expression into a register
            uint8_t x_reg = allocate_register();
            saved = current_result_reg;
            current_result_reg = x_reg;
            node.x_expr->accept(*this);
            current_result_reg = saved;

            // Evaluate y coordinate expression into a register
            uint8_t y_reg = allocate_register();
            saved = current_result_reg;
            current_result_reg = y_reg;
            node.y_expr->accept(*this);
            current_result_reg = saved;

            // Use a safe address for BCD storage at end of memory (0xFD0-0xFD2)
            // This avoids corrupting program code which can extend past 0xEF0 for large ROMs
            emit_opcode(0xAFD0); // LD I, 0xFD0

            // Fx33 - store BCD of Vx at I, I+1, I+2
            emit_opcode(0xF033 | (val_reg << 8)); // LD B, Vx

            // Draw hundreds digit
            emit_opcode(0xF065); // LD V0, [I] - load BCD digits into V0
            emit_opcode(0xF029); // LD F, V0 - point I to font for digit
            emit_opcode(0xD000 | (x_reg << 8) | (y_reg << 4) | 0x5); // DRW Vx, Vy, 5

            // Move x += 5 for tens digit
            emit_opcode(0x7005 | (x_reg << 8)); // ADD Vx, 5

            // Load tens digit
            emit_opcode(0xAFD1); // LD I, 0xFD1
            emit_opcode(0xF065); // LD V0, [I]
            emit_opcode(0xF029); // LD F, V0
            emit_opcode(0xD000 | (x_reg << 8) | (y_reg << 4) | 0x5); // DRW

            // Move x += 5 for units digit
            emit_opcode(0x7005 | (x_reg << 8)); // ADD Vx, 5

            // Load units digit
            emit_opcode(0xAFD2); // LD I, 0xFD2
            emit_opcode(0xF065); // LD V0, [I]
            emit_opcode(0xF029); // LD F, V0
            emit_opcode(0xD000 | (x_reg << 8) | (y_reg << 4) | 0x5); // DRW

            // Restore x position (subtract 10)
            emit_opcode(0x70F6 | (x_reg << 8)); // ADD Vx, -10 (0xF6 = -10)

            free_register(val_reg);
            free_register(x_reg);
            free_register(y_reg);
        }

        void CodeGenerator::visit(ArrayDeclNode& node) {
            // Check if array is already declared
            if (arrays.find(node.name) != arrays.end()) {
                errorHandler.error("Array '" + node.name + "' already declared", node.line, node.column);
                return;
            }

            // Allocate memory for the array
            arrays[node.name] = next_array_addr;
            array_sizes[node.name] = node.size;
            array_dims[node.name] = node.dimensions;
            next_array_addr += static_cast<uint16_t>(node.size);

            // Check for memory overflow - arrays share space with runtime stack
            // Runtime stack starts at CALLER_SAVE_BASE (0xF50), so arrays should stay below that
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

        void CodeGenerator::visit(ArrayAccessExprNode& node) {
            // arr[i] or arr[i][j] - read array element into current_result_reg
            auto it = arrays.find(node.array_name);
            if (it == arrays.end()) {
                errorHandler.error("Undefined array: " + node.array_name, node.line, node.column);
                return;
            }
            uint16_t base_addr = it->second;
            auto& dims = array_dims[node.array_name];

            // Compute linear index from multi-dimensional indices
            // For arr[d0][d1][d2] with access arr[i][j][k]: offset = i*d1*d2 + j*d2 + k
            uint8_t idx_reg = 0; // Use V0 for final index
            uint8_t saved = current_result_reg;

            if (node.indices.size() == 1) {
                // Simple 1D access
                current_result_reg = idx_reg;
                node.indices[0]->accept(*this);
                current_result_reg = saved;
            } else {
                // Multi-dimensional: compute linear offset
                // Start with first index
                current_result_reg = idx_reg;
                node.indices[0]->accept(*this);
                current_result_reg = saved;

                // For each subsequent index, multiply by dimension and add
                for (size_t i = 1; i < node.indices.size() && i < dims.size(); i++) {
                    // idx_reg = idx_reg * dims[i]
                    uint8_t mult_reg = allocate_register();
                    emit_opcode(0x6000 | (mult_reg << 8) | (dims[i] & 0xFF)); // LD mult_reg, dims[i]
                    
                    // Multiply using repeated addition
                    uint8_t temp_reg = allocate_register();
                    emit_opcode(0x8000 | (temp_reg << 8) | (idx_reg << 4) | 0x0); // LD temp, idx
                    emit_opcode(0x6000 | (idx_reg << 8) | 0x00); // LD idx, 0
                    uint16_t mult_loop = static_cast<uint16_t>(output.size());
                    emit_opcode(0x3000 | (mult_reg << 8) | 0x00); // SE mult_reg, 0
                    uint16_t mult_end = output.size();
                    emit_jump(0); // placeholder
                    emit_opcode(0x8000 | (idx_reg << 8) | (temp_reg << 4) | 0x4); // ADD idx, temp
                    emit_opcode(0x7000 | (mult_reg << 8) | 0xFF); // ADD mult_reg, -1
                    emit_jump(mult_loop);
                    patch_jump_at(mult_end, static_cast<uint16_t>(output.size()));
                    free_register(mult_reg);
                    free_register(temp_reg);

                    // Add current index
                    uint8_t cur_idx_reg = allocate_register();
                    current_result_reg = cur_idx_reg;
                    node.indices[i]->accept(*this);
                    current_result_reg = saved;
                    emit_opcode(0x8000 | (idx_reg << 8) | (cur_idx_reg << 4) | 0x4); // ADD idx, cur_idx
                    free_register(cur_idx_reg);
                }
            }

            // Set I = base_addr
            emit_opcode(0xA000 | (base_addr & 0x0FFF)); // LD I, base_addr

            // Add index to I: I = I + V0 (via Fx1E)
            emit_opcode(0xF01E | (idx_reg << 8)); // ADD I, V0

            // Load value from [I] into V0, then copy to current_result_reg
            emit_opcode(0xF065 | (0 << 8)); // LD V0, [I] (loads V0 from memory at I)

            // Copy V0 to current_result_reg if different
            if (current_result_reg != 0) {
                emit_opcode(0x8000 | (current_result_reg << 8) | (0 << 4) | 0x0); // LD Vx, V0
            }
        }

        void CodeGenerator::visit(ArrayAssignmentNode& node) {
            // arr[i] = x; or arr[i][j] = x; - write value to array element
            auto it = arrays.find(node.array_name);
            if (it == arrays.end()) {
                errorHandler.error("Undefined array: " + node.array_name, node.line, node.column);
                return;
            }
            uint16_t base_addr = it->second;
            auto& dims = array_dims[node.array_name];

            // Evaluate value into V0
            uint8_t val_reg = 0; // Use V0 for value
            uint8_t saved = current_result_reg;
            current_result_reg = val_reg;
            node.value->accept(*this);
            current_result_reg = saved;

            // Save V0 to a temp register while computing index
            uint8_t saved_val_reg = allocate_register();
            emit_opcode(0x8000 | (saved_val_reg << 8) | (val_reg << 4) | 0x0); // LD saved_val, V0

            // Compute linear index from multi-dimensional indices into idx_reg
            uint8_t idx_reg = allocate_register();

            if (node.indices.size() == 1) {
                // Simple 1D access
                saved = current_result_reg;
                current_result_reg = idx_reg;
                node.indices[0]->accept(*this);
                current_result_reg = saved;
            } else {
                // Multi-dimensional: compute linear offset
                saved = current_result_reg;
                current_result_reg = idx_reg;
                node.indices[0]->accept(*this);
                current_result_reg = saved;

                for (size_t i = 1; i < node.indices.size() && i < dims.size(); i++) {
                    // idx_reg = idx_reg * dims[i] + index[i]
                    uint8_t mult_reg = allocate_register();
                    emit_opcode(0x6000 | (mult_reg << 8) | (dims[i] & 0xFF)); // LD mult_reg, dims[i]
                    
                    uint8_t temp_reg = allocate_register();
                    emit_opcode(0x8000 | (temp_reg << 8) | (idx_reg << 4) | 0x0); // LD temp, idx
                    emit_opcode(0x6000 | (idx_reg << 8) | 0x00); // LD idx, 0
                    uint16_t mult_loop = static_cast<uint16_t>(output.size());
                    emit_opcode(0x3000 | (mult_reg << 8) | 0x00); // SE mult_reg, 0
                    uint16_t mult_end = output.size();
                    emit_jump(0);
                    emit_opcode(0x8000 | (idx_reg << 8) | (temp_reg << 4) | 0x4); // ADD idx, temp
                    emit_opcode(0x7000 | (mult_reg << 8) | 0xFF); // ADD mult_reg, -1
                    emit_jump(mult_loop);
                    patch_jump_at(mult_end, static_cast<uint16_t>(output.size()));
                    free_register(mult_reg);
                    free_register(temp_reg);

                    uint8_t cur_idx_reg = allocate_register();
                    saved = current_result_reg;
                    current_result_reg = cur_idx_reg;
                    node.indices[i]->accept(*this);
                    current_result_reg = saved;
                    emit_opcode(0x8000 | (idx_reg << 8) | (cur_idx_reg << 4) | 0x4); // ADD idx, cur_idx
                    free_register(cur_idx_reg);
                }
            }

            // Restore value to V0
            emit_opcode(0x8000 | (val_reg << 8) | (saved_val_reg << 4) | 0x0); // LD V0, saved_val
            free_register(saved_val_reg);

            // Set I = base_addr
            emit_opcode(0xA000 | (base_addr & 0x0FFF)); // LD I, base_addr

            // Add index to I
            emit_opcode(0xF01E | (idx_reg << 8)); // ADD I, idx_reg

            // Store V0 at [I]
            emit_opcode(0xF055 | (0 << 8)); // LD [I], V0

            free_register(idx_reg);
        }

        void CodeGenerator::visit(ReturnNode& node) {
            // If there's a return value, evaluate it into V0
            if (node.value) {
                uint8_t saved = current_result_reg;
                current_result_reg = 0; // Return value in V0
                node.value->accept(*this);
                current_result_reg = saved;
            }
            // RET instruction
            emit_opcode(0x00EE);
        }

        void CodeGenerator::visit(FunctionCallExprNode& node) {
            // Function call in expression context - expects return value
            // Uses runtime call stack via VD register for recursion support
            uint8_t caller_max = getMaxLocalVariableReg();
            
            // Smart caller-save: only save registers that might be clobbered
            uint8_t save_max = caller_max;
            auto callee_it = function_max_regs.find(node.function_name);
            if (callee_it != function_max_regs.end()) {
                save_max = std::min(caller_max, callee_it->second);
            }
            
            if (save_max > 0) {
                emit_opcode(0xA000 | (CALLER_SAVE_BASE & 0x0FFF)); // LD I, CALLER_SAVE_BASE
                emit_opcode(0xFD1E);                               // ADD I, VD
                emit_opcode(0x7D11);                               // ADD VD, 17
                emit_opcode(0xF055 | (save_max << 8));             // LD [I], Vsave_max
            } else {
                emit_opcode(0x7D11);                               // ADD VD, 17 (still need to advance stack)
            }

            // Load arguments into V1, V2, V3, etc.
            for (size_t i = 0; i < node.arguments.size() && i < 14; i++) {
                uint8_t arg_reg = static_cast<uint8_t>(i + 1); // V1, V2, etc.
                if (arg_reg == 0xD) continue;  // Skip VD
                uint8_t saved = current_result_reg;
                current_result_reg = arg_reg;
                node.arguments[i]->accept(*this);
                current_result_reg = saved;
            }

            // CALL function
            uint16_t call_at = static_cast<uint16_t>(output.size());
            emit_opcode(0x2000); // placeholder CALL
            fixups.push_back({call_at, node.function_name, true});

            // Restore: ADD VD, -17; LD I, CALLER_SAVE_BASE; ADD I, VD
            emit_opcode(0x7DEF);                               // ADD VD, -17 (0xEF = -17 in 8-bit)

            // Save return value (V0) to a temp location before restoring registers
            // Use address right after the register save area
            emit_opcode(0xA000 | ((CALLER_SAVE_BASE + 16) & 0x0FFF)); // LD I, temp_addr
            emit_opcode(0xFD1E);                                      // ADD I, VD
            emit_opcode(0xF055 | (0 << 8));                           // LD [I], V0

            // Restore local variable registers (same as saved)
            if (save_max > 0) {
                emit_opcode(0xA000 | (CALLER_SAVE_BASE & 0x0FFF)); // LD I, CALLER_SAVE_BASE
                emit_opcode(0xFD1E);                               // ADD I, VD
                emit_opcode(0xF065 | (save_max << 8));             // LD Vsave_max, [I]
            }

            // Load return value from temp location into current_result_reg
            emit_opcode(0xA000 | ((CALLER_SAVE_BASE + 16) & 0x0FFF)); // LD I, temp_addr
            emit_opcode(0xFD1E);                                      // ADD I, VD
            emit_opcode(0xF065 | (0 << 8));                           // LD V0, [I]
            if (current_result_reg != 0) {
                emit_opcode(0x8000 | (current_result_reg << 8) | (0 << 4) | 0x0); // LD Vx, V0
            }
        }

        void CodeGenerator::visit(FunctionCallNode& node) {
            // Function call in statement context - no return value expected
            // Uses runtime call stack via VD register for recursion support
            uint8_t caller_max = getMaxLocalVariableReg();
            
            // Smart caller-save: only save registers that might be clobbered
            uint8_t save_max = caller_max;
            auto callee_it = function_max_regs.find(node.function_name);
            if (callee_it != function_max_regs.end()) {
                save_max = std::min(caller_max, callee_it->second);
            }

            // Save local variable registers using runtime stack offset (VD)
            if (save_max > 0) {
                emit_opcode(0xA000 | (CALLER_SAVE_BASE & 0x0FFF)); // LD I, CALLER_SAVE_BASE
                emit_opcode(0xFD1E);                               // ADD I, VD
                emit_opcode(0x7D11);                               // ADD VD, 17
                emit_opcode(0xF055 | (save_max << 8));             // LD [I], Vsave_max
            } else {
                emit_opcode(0x7D11);                               // ADD VD, 17
            }

            // Load arguments into V1, V2, V3, etc.
            for (size_t i = 0; i < node.arguments.size() && i < 14; i++) {
                uint8_t arg_reg = static_cast<uint8_t>(i + 1); // V1, V2, etc.
                if (arg_reg == 0xD) continue;  // Skip VD
                uint8_t saved = current_result_reg;
                current_result_reg = arg_reg;
                node.arguments[i]->accept(*this);
                current_result_reg = saved;
            }

            // CALL function
            uint16_t call_at = static_cast<uint16_t>(output.size());
            emit_opcode(0x2000); // placeholder CALL
            fixups.push_back({call_at, node.function_name, true});

            // Restore: ADD VD, -17; LD I, CALLER_SAVE_BASE; ADD I, VD; LD Vmax, [I]
            emit_opcode(0x7DEF);                               // ADD VD, -17

            if (save_max > 0) {
                emit_opcode(0xA000 | (CALLER_SAVE_BASE & 0x0FFF)); // LD I, CALLER_SAVE_BASE
                emit_opcode(0xFD1E);                               // ADD I, VD
                emit_opcode(0xF065 | (save_max << 8));             // LD Vsave_max, [I]
            }
        }

        // CHIP-8 memory limits
        static constexpr size_t MAX_ROM_SIZE = 0xDFF - 0x200 + 1;  // 3584 bytes (0x200-0xDFF)
        static constexpr size_t ROM_WARNING_THRESHOLD = MAX_ROM_SIZE * 90 / 100;  // Warn at 90%
        bool rom_size_warning_issued = false;

        void CodeGenerator::emit_byte(uint8_t byte) {
            output.push_back(byte);
            
            // Check ROM size limits
            if (output.size() > MAX_ROM_SIZE) {
                errorHandler.error("ROM size exceeds CHIP-8 limit of 3584 bytes (" + 
                                   std::to_string(output.size()) + " bytes)", 0, 0);
            } else if (!rom_size_warning_issued && output.size() > ROM_WARNING_THRESHOLD) {
                errorHandler.warning("ROM size is at " + std::to_string(output.size() * 100 / MAX_ROM_SIZE) + 
                                    "% capacity (" + std::to_string(output.size()) + "/" + 
                                    std::to_string(MAX_ROM_SIZE) + " bytes)", 0, 0);
                rom_size_warning_issued = true;
            }
        }

        void CodeGenerator::emit_opcode(uint16_t opcode) {
            emit_byte(static_cast<uint8_t>((opcode >> 8) & 0xFF));
            emit_byte(static_cast<uint8_t>(opcode & 0xFF));
        }

        void CodeGenerator::emit_jump(uint16_t addr_offset_bytes) {
            // JP absolute address (0x200 + byte offset)
            uint16_t target_addr = static_cast<uint16_t>(0x200 + (addr_offset_bytes & 0x0FFF));
            emit_opcode(0x1000 | (target_addr & 0x0FFF));
        }

        void CodeGenerator::patch_jump_at(uint16_t at_offset_bytes, uint16_t target_offset_bytes) {
            uint16_t target_addr = static_cast<uint16_t>(0x200 + (target_offset_bytes & 0x0FFF));
            uint16_t opcode = 0x1000 | (target_addr & 0x0FFF);
            output[at_offset_bytes]     = static_cast<uint8_t>((opcode >> 8) & 0xFF);
            output[at_offset_bytes + 1] = static_cast<uint8_t>(opcode & 0xFF);
        }

        uint8_t CodeGenerator::allocate_register() {
            // Count currently used registers for pressure warning
            int used_count = 0;
            for (uint8_t i = 1; i < 15; i++) {
                if (i != 0xD && used_registers[i]) used_count++;
            }
            
            for (uint8_t i = 1; i < 15; i++) { // Skip V0 and VF
                if (i == 0xD) continue;  // Skip VD - reserved for runtime call stack
                if (!used_registers[i]) {
                    used_registers[i] = true;
                    
                    // Warn when approaching register limit (13 usable: V1-VC, VE)
                    if (used_count >= 11) {  // 11+ of 13 = 85%+ usage
                        errorHandler.warning("High register pressure: " + std::to_string(used_count + 1) + 
                                           "/13 registers in use. Consider using globals or simplifying expressions.", 0, 0);
                    }
                    return i;
                }
            }
            // Build a diagnostic showing which registers are variables vs temps
            std::string vars_desc;
            for (const auto& [name, var] : variables) {
                if (!vars_desc.empty()) vars_desc += ", ";
                vars_desc += name;
            }
            errorHandler.error("Out of registers (13 available per function; all in use by locals/temporaries). "
                               "Locals: [" + vars_desc + "]. "
                               "Use 'global' for additional storage or simplify nested expressions.", 0, 0);
            return 0; // Default to V0, but ideally this should never happen
        }

        void CodeGenerator::free_register(uint8_t reg) {
            if (reg > 0 && reg < 15) {
                used_registers[reg] = false;
            }
        }

        uint8_t CodeGenerator::get_variable_register(const std::string& name) {
            auto it = variables.find(name);
            if (it == variables.end()) {
                errorHandler.error("Undefined variable: " + name, 0, 0);
                return 0; // Default to V0, but should not happen
            }
            return it->second.reg;
        }

        uint8_t CodeGenerator::getMaxLocalVariableReg() {
            // Return the highest register that is currently in use
            // This includes both local variables AND temp registers from expression evaluation
            uint8_t max_reg = 0;
            for (uint8_t i = 1; i < 15; i++) {
                if (used_registers[i]) {
                    max_reg = i;
                }
            }
            return max_reg;
        }

        void CodeGenerator::emit_comparison_node(uint8_t dest_reg, uint8_t left_reg, uint8_t right_reg, TokenType op) {
            // Ensure dest_reg will contain 0 (false) or 1 (true)
            switch (op) {
                case TokenType::EQUALS:
                    // EQUALS: result = 1 if left == right, 0 otherwise
                    // Uses the native register-compare skip; does not clobber operands
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x00);            // LD dest, 0
                    emit_opcode(0x9000 | (left_reg << 8) | (right_reg << 4)); // SNE left, right -> skip if !=
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x01);            // LD dest, 1 (equal case)
                    break;
                case TokenType::NOT_EQUALS:
                    // NOT_EQUALS: result = 1 if left != right, 0 otherwise
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x00);            // LD dest, 0
                    emit_opcode(0x5000 | (left_reg << 8) | (right_reg << 4)); // SE left, right -> skip if ==
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x01);            // LD dest, 1 (not equal case)
                    break;
                case TokenType::LESS_THAN:
                    // dest = left; SUB dest, right; VF=0 if borrow (left<right);
                    emit_opcode(0x8000 | (dest_reg << 8) | (left_reg << 4) | 0x0); // LD dest, left
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x5); // SUB dest, right
                    // VF==0 => true; move VF (0/1) into dest as 1/0 via: dest = 1 - VF
                    // Load dest=1, then SUB dest, VF
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x01); // LD dest, 1
                    emit_opcode(0x8000 | (dest_reg << 8) | (0xF << 4) | 0x5); // SUB dest, VF
                    break;
                case TokenType::GREATER_THAN:
                    // dest = right; SUB dest, left; VF=0 if borrow (right<left) => left>right is true when VF==0
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x0); // LD dest, right
                    emit_opcode(0x8000 | (dest_reg << 8) | (left_reg << 4) | 0x5);  // SUB dest, left
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x01); // LD dest, 1
                    emit_opcode(0x8000 | (dest_reg << 8) | (0xF << 4) | 0x5); // SUB dest, VF (1-VF)
                    break;
                case TokenType::LESS_EQUAL:
                    // left <= right is !(left > right), i.e., NOT(right < left)
                    // right < left means VF=0 after right - left
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x0); // LD dest, right
                    emit_opcode(0x8000 | (dest_reg << 8) | (left_reg << 4) | 0x5);  // SUB dest, left
                    // VF=1 means no borrow (right >= left), so left <= right is true when VF=1
                    emit_opcode(0x8000 | (dest_reg << 8) | (0xF << 4) | 0x0); // LD dest, VF
                    break;
                case TokenType::GREATER_EQUAL:
                    // left >= right means VF=1 after left - right (no borrow)
                    emit_opcode(0x8000 | (dest_reg << 8) | (left_reg << 4) | 0x0); // LD dest, left
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x5); // SUB dest, right
                    // VF=1 means no borrow (left >= right)
                    emit_opcode(0x8000 | (dest_reg << 8) | (0xF << 4) | 0x0); // LD dest, VF
                    break;
                default:
                    errorHandler.error("Invalid comparison operator", 0, 0);
                    break;
            }
        }

        void CodeGenerator::process_binary_operation(uint8_t dest_reg, uint8_t left_reg, uint8_t right_reg, TokenType op) {
            // First, copy the left operand to the destination register
            if (dest_reg != left_reg) {
                emit_opcode(0x8000 | (dest_reg << 8) | (left_reg << 4) | 0x0); // LD Vx, Vy
            }

            // Then perform the operation
            switch (op) {
                case TokenType::PLUS:
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x4); // ADD Vx, Vy
                    break;

                case TokenType::MINUS:
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x5); // SUB Vx, Vy
                    break;

                case TokenType::MULTIPLY:
                {
                    uint8_t temp_reg = allocate_register();
                    uint8_t counter_reg = allocate_register();

                    // Initialize result to 0
                    emit_opcode(0x6000 | (dest_reg << 8) | 0x00); // LD Vx, 0

                    // Copy right operand to counter
                    emit_opcode(0x8000 | (counter_reg << 8) | (right_reg << 4) | 0x0); // LD Vx, Vy

                    // Copy left operand to temp
                    emit_opcode(0x8000 | (temp_reg << 8) | (left_reg << 4) | 0x0); // LD Vx, Vy

                    // Start of loop
                    uint16_t loop_start = static_cast<uint16_t>(output.size());

                    // if counter != 0 -> skip the jump to end (continue looping)
                    emit_opcode(0x4000 | (counter_reg << 8) | 0x00); // SNE Vcounter, 0
                    uint16_t jump_to_end_at = output.size();
                    emit_jump(0); // placeholder - exit if counter == 0

                    // Add temp to result
                    emit_opcode(0x8000 | (dest_reg << 8) | (temp_reg << 4) | 0x4); // ADD Vx, Vy

                    // Decrement counter
                    emit_opcode(0x7000 | (counter_reg << 8) | 0xFF); // ADD Vx, -1

                    // Jump back to start
                    emit_jump(loop_start);

                    // Patch end target
                    patch_jump_at(jump_to_end_at, static_cast<uint16_t>(output.size()));

                    // Free temporary registers
                    free_register(temp_reg);
                    free_register(counter_reg);
                }
                    break;

                case TokenType::DIVIDE:
                {
                    uint8_t temp_reg = allocate_register();
                    uint8_t counter_reg = allocate_register();

                    // Initialize result (counter) to 0
                    emit_opcode(0x6000 | (counter_reg << 8) | 0x00); // LD Vcounter, 0

                    // Copy left operand to temp (dividend)
                    emit_opcode(0x8000 | (temp_reg << 8) | (left_reg << 4) | 0x0); // LD temp, left

                    // Start of loop
                    uint16_t loop_start = static_cast<uint16_t>(output.size());

                    // If temp >= right -> skip the jump, continue dividing
                    // SUB temp, right sets VF=1 if no borrow (temp >= right)
                    emit_opcode(0x8000 | (0xF << 8) | (temp_reg << 4) | 0x0); // LD VF, temp
                    emit_opcode(0x8000 | (0xF << 8) | (right_reg << 4) | 0x5); // SUB VF, right (VF=1 if temp>=right)
                    emit_opcode(0x4000 | (0xF << 8) | 0x00);                  // SNE VF, 0 -> skip next if temp>=right
                    uint16_t jump_to_end_at = output.size();
                    emit_jump(0); // placeholder - exit if temp < right

                    // temp -= right
                    emit_opcode(0x8000 | (temp_reg << 8) | (right_reg << 4) | 0x5); // SUB temp, right

                    // counter++
                    emit_opcode(0x7000 | (counter_reg << 8) | 0x01); // ADD counter, 1

                    // loop
                    emit_jump(loop_start);

                    // Patch end target - jump here when temp < right
                    patch_jump_at(jump_to_end_at, static_cast<uint16_t>(output.size()));

                    // Copy result to destination
                    emit_opcode(0x8000 | (dest_reg << 8) | (counter_reg << 4) | 0x0); // LD dest, counter

                    // Free temporary registers
                    free_register(temp_reg);
                    free_register(counter_reg);
                }
                    break;

                case TokenType::AMPERSAND: // Bitwise AND
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x2); // AND Vx, Vy
                    break;

                case TokenType::PIPE: // Bitwise OR
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x1); // OR Vx, Vy
                    break;

                case TokenType::CARET: // Bitwise XOR
                    emit_opcode(0x8000 | (dest_reg << 8) | (right_reg << 4) | 0x3); // XOR Vx, Vy
                    break;

                default:
                    errorHandler.error("Invalid binary operator", 0, 0);
                    break;
            }
        }

        void CodeGenerator::visit(ConstDeclNode& node) {
            // Store constant value for later use in expressions
            constants[node.name] = node.value;
        }

        void CodeGenerator::visit(EnumDeclNode& node) {
            // Store each enum value as a constant
            // Format: EnumName.ValueName or just ValueName
            for (const auto& ev : node.values) {
                // Store as EnumName.ValueName
                constants[node.name + "." + ev.name] = ev.value;
                // Also store just ValueName for convenience (if no collision)
                if (constants.find(ev.name) == constants.end()) {
                    constants[ev.name] = ev.value;
                }
            }
        }

        void CodeGenerator::visit(InlineAsmNode& node) {
            // Emit raw opcodes directly
            for (uint16_t opcode : node.opcodes) {
                emit_opcode(opcode);
            }
        }

        void CodeGenerator::visit(GlobalVarDeclNode& node) {
            // Global variables are stored in memory (like arrays)
            // Allocate memory address for the variable
            if (arrays.find(node.name) != arrays.end()) {
                errorHandler.error("Global variable '" + node.name + "' already declared", node.line, node.column);
                return;
            }
            
            arrays[node.name] = next_array_addr;
            array_sizes[node.name] = 1;  // Single byte
            next_array_addr += 1;
            
            // If there's an initializer, emit code to store the value
            if (node.initializer) {
                // Evaluate initializer into V0
                uint8_t saved = current_result_reg;
                current_result_reg = 0;
                node.initializer->accept(*this);
                current_result_reg = saved;
                
                // Store V0 at the global variable's address
                emit_opcode(0xA000 | (arrays[node.name] & 0x0FFF)); // LD I, addr
                emit_opcode(0xF055 | (0 << 8)); // LD [I], V0
            }
        }

        void CodeGenerator::visit(LogicalExprNode& node) {
            // Short-circuit evaluation for && and ||
            // Result is 0 (false) or 1 (true) in current_result_reg
            
            // Evaluate left side
            uint8_t left_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = left_reg;
            node.left->accept(*this);
            current_result_reg = saved;

            if (node.op == TokenType::AND_AND) {
                // AND: if left is 0, result is 0 (skip right)
                // If left != 0, skip the short-circuit
                emit_opcode(0x4000 | (left_reg << 8) | 0x00); // SNE left, 0 -> skip next if left != 0
                uint16_t short_circuit_at = output.size();
                emit_jump(0); // placeholder - jump to set result=0 if left == 0

                // Evaluate right side (left was true)
                uint8_t right_reg = allocate_register();
                saved = current_result_reg;
                current_result_reg = right_reg;
                node.right->accept(*this);
                current_result_reg = saved;

                // Result is right's truthiness: 1 if right != 0, else 0
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x00); // LD result, 0
                emit_opcode(0x3000 | (right_reg << 8) | 0x00); // SE right, 0 -> skip next if right == 0
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x01); // LD result, 1
                
                uint16_t end_at = output.size();
                emit_jump(0); // jump to end

                // Short-circuit path: result = 0
                patch_jump_at(short_circuit_at, static_cast<uint16_t>(output.size()));
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x00); // LD result, 0

                // End
                patch_jump_at(end_at, static_cast<uint16_t>(output.size()));

                free_register(right_reg);
            } else { // OR_OR
                // OR: if left is non-zero, result is 1 (skip right)
                // If left == 0, skip the short-circuit
                emit_opcode(0x3000 | (left_reg << 8) | 0x00); // SE left, 0 -> skip next if left == 0
                uint16_t short_circuit_at = output.size();
                emit_jump(0); // placeholder - jump to set result=1 if left != 0

                // Evaluate right side (left was false)
                uint8_t right_reg = allocate_register();
                saved = current_result_reg;
                current_result_reg = right_reg;
                node.right->accept(*this);
                current_result_reg = saved;

                // Result is right's truthiness: 1 if right != 0, else 0
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x00); // LD result, 0
                emit_opcode(0x3000 | (right_reg << 8) | 0x00); // SE right, 0 -> skip next if right == 0
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x01); // LD result, 1
                
                uint16_t end_at = output.size();
                emit_jump(0); // jump to end

                // Short-circuit path: result = 1
                patch_jump_at(short_circuit_at, static_cast<uint16_t>(output.size()));
                emit_opcode(0x6000 | (current_result_reg << 8) | 0x01); // LD result, 1

                // End
                patch_jump_at(end_at, static_cast<uint16_t>(output.size()));

                free_register(right_reg);
            }

            free_register(left_reg);
        }

        void CodeGenerator::visit(SwitchNode& node) {
            // Evaluate switch expression into a register
            uint8_t expr_reg = allocate_register();
            uint8_t saved = current_result_reg;
            current_result_reg = expr_reg;
            node.expr->accept(*this);
            current_result_reg = saved;

            std::vector<uint16_t> case_body_addrs;  // Addresses of case bodies
            std::vector<uint16_t> jump_to_case_addrs; // Addresses of jumps to case bodies
            std::vector<uint16_t> end_jumps; // Jumps to end after each case body
            uint16_t default_addr = 0;
            bool has_default = false;

            // First pass: emit comparison jumps for each case
            for (size_t i = 0; i < node.cases.size(); i++) {
                const auto& c = node.cases[i];
                if (c.is_default) {
                    has_default = true;
                    // Default is handled after all cases
                    continue;
                }
                
                // Compare expr_reg with case value
                // SNE expr, value -> skip next if NOT equal (so we jump when equal)
                emit_opcode(0x4000 | (expr_reg << 8) | (c.value & 0xFF)); // SNE expr, value
                // If not equal, skip the jump; if equal, take the jump to case body
                uint16_t jump_at = output.size();
                emit_jump(0); // placeholder - jump to case body if equal
                jump_to_case_addrs.push_back(jump_at);
            }

            // Jump to default or end if no case matched
            uint16_t default_or_end_jump = output.size();
            emit_jump(0); // placeholder

            // Second pass: emit case bodies
            size_t jump_idx = 0;
            for (size_t i = 0; i < node.cases.size(); i++) {
                const auto& c = node.cases[i];
                
                if (c.is_default) {
                    default_addr = static_cast<uint16_t>(output.size());
                } else {
                    // Patch the jump to this case body
                    patch_jump_at(jump_to_case_addrs[jump_idx], static_cast<uint16_t>(output.size()));
                    jump_idx++;
                }
                
                // Emit case body
                for (const auto& stmt : c.statements) {
                    stmt->accept(*this);
                }
                
                // Jump to end (implicit break)
                uint16_t end_jump = output.size();
                emit_jump(0);
                end_jumps.push_back(end_jump);
            }

            // Patch default/end jump
            if (has_default) {
                patch_jump_at(default_or_end_jump, default_addr);
            } else {
                patch_jump_at(default_or_end_jump, static_cast<uint16_t>(output.size()));
            }

            // Patch all end jumps
            uint16_t end_addr = static_cast<uint16_t>(output.size());
            for (uint16_t addr : end_jumps) {
                patch_jump_at(addr, end_addr);
            }

            free_register(expr_reg);
        }

        void CodeGenerator::visit(EntityDefNode& node) {
            // Register the entity type
            EntityType type;
            for (const auto& field : node.fields) {
                type.fields.push_back(field);
            }
            type.size = node.size;
            entity_types[node.name] = type;
        }

        void CodeGenerator::visit(EntityDeclNode& node) {
            // Look up the entity type
            auto type_it = entity_types.find(node.type_name);
            if (type_it == entity_types.end()) {
                errorHandler.error("Unknown entity type: " + node.type_name, node.line, node.column);
                return;
            }

            // Calculate total size
            int entity_size = type_it->second.size;
            int total_size = (node.array_size > 0) ? entity_size * node.array_size : entity_size;

            // Allocate memory for the entity
            EntityInstance instance;
            instance.type_name = node.type_name;
            instance.base_addr = next_array_addr;
            instance.array_size = node.array_size;

            entity_instances[node.var_name] = instance;
            next_array_addr += static_cast<uint16_t>(total_size);

            // Check for memory overflow
            if (next_array_addr > 0xFFF) {
                errorHandler.error("Entity allocation exceeds available memory", node.line, node.column);
            }
        }

        void CodeGenerator::visit(EntityFieldAccessExpr& node) {
            // Look up the entity instance
            auto inst_it = entity_instances.find(node.entity_name);
            if (inst_it == entity_instances.end()) {
                errorHandler.error("Unknown entity variable: " + node.entity_name, node.line, node.column);
                return;
            }

            // Look up the entity type
            auto type_it = entity_types.find(inst_it->second.type_name);
            if (type_it == entity_types.end()) {
                errorHandler.error("Unknown entity type: " + inst_it->second.type_name, node.line, node.column);
                return;
            }

            // Find the field offset
            int field_offset = -1;
            for (const auto& field : type_it->second.fields) {
                if (field.name == node.field_name) {
                    field_offset = field.offset;
                    break;
                }
            }
            if (field_offset < 0) {
                errorHandler.error("Unknown field '" + node.field_name + "' in entity '" + inst_it->second.type_name + "'", node.line, node.column);
                return;
            }

            uint16_t base_addr = inst_it->second.base_addr;
            int entity_size = type_it->second.size;

            // Compute address: base + (index * entity_size) + field_offset
            uint8_t idx_reg = allocate_register();

            if (node.index) {
                // Array access: entity[i].field
                uint8_t saved = current_result_reg;
                current_result_reg = idx_reg;
                node.index->accept(*this);
                current_result_reg = saved;

                // Multiply by entity size if > 1
                if (entity_size > 1) {
                    uint8_t size_reg = allocate_register();
                    emit_opcode(0x6000 | (size_reg << 8) | (entity_size & 0xFF)); // LD size_reg, entity_size

                    // Multiply idx_reg by size_reg using repeated addition
                    uint8_t temp_reg = allocate_register();
                    emit_opcode(0x8000 | (temp_reg << 8) | (idx_reg << 4) | 0x0); // LD temp, idx
                    emit_opcode(0x6000 | (idx_reg << 8) | 0x00); // LD idx, 0
                    uint16_t mult_loop = static_cast<uint16_t>(output.size());
                    emit_opcode(0x4000 | (size_reg << 8) | 0x00); // SNE size_reg, 0 - skip JP if size != 0
                    uint16_t mult_end = output.size();
                    emit_jump(0); // placeholder - exit when size == 0
                    emit_opcode(0x8000 | (idx_reg << 8) | (temp_reg << 4) | 0x4); // ADD idx, temp
                    emit_opcode(0x7000 | (size_reg << 8) | 0xFF); // ADD size_reg, -1
                    emit_jump(mult_loop);
                    patch_jump_at(mult_end, static_cast<uint16_t>(output.size()));
                    free_register(temp_reg);
                    free_register(size_reg);
                }

                // Add field offset
                if (field_offset > 0) {
                    emit_opcode(0x7000 | (idx_reg << 8) | (field_offset & 0xFF)); // ADD idx_reg, field_offset
                }
            } else {
                // Single entity: just use field offset
                emit_opcode(0x6000 | (idx_reg << 8) | (field_offset & 0xFF)); // LD idx_reg, field_offset
            }

            // Load I = base_addr
            emit_opcode(0xA000 | (base_addr & 0x0FFF)); // LD I, base_addr

            // Add index to I
            emit_opcode(0xF01E | (idx_reg << 8)); // ADD I, idx_reg

            // Load value from [I] into V0, then copy to current_result_reg
            emit_opcode(0xF065 | (0 << 8)); // LD V0, [I]

            // Copy V0 to current_result_reg if different
            if (current_result_reg != 0) {
                emit_opcode(0x8000 | (current_result_reg << 8) | (0 << 4) | 0x0); // LD Vx, V0
            }

            free_register(idx_reg);
        }

        void CodeGenerator::visit(EntityFieldAssignNode& node) {
            // Look up the entity instance
            auto inst_it = entity_instances.find(node.entity_name);
            if (inst_it == entity_instances.end()) {
                errorHandler.error("Unknown entity variable: " + node.entity_name, node.line, node.column);
                return;
            }

            // Look up the entity type
            auto type_it = entity_types.find(inst_it->second.type_name);
            if (type_it == entity_types.end()) {
                errorHandler.error("Unknown entity type: " + inst_it->second.type_name, node.line, node.column);
                return;
            }

            // Find the field offset
            int field_offset = -1;
            for (const auto& field : type_it->second.fields) {
                if (field.name == node.field_name) {
                    field_offset = field.offset;
                    break;
                }
            }
            if (field_offset < 0) {
                errorHandler.error("Unknown field '" + node.field_name + "' in entity '" + inst_it->second.type_name + "'", node.line, node.column);
                return;
            }

            uint16_t base_addr = inst_it->second.base_addr;
            int entity_size = type_it->second.size;

            // Evaluate value into V0
            uint8_t val_reg = 0;
            uint8_t saved = current_result_reg;
            current_result_reg = val_reg;
            node.value->accept(*this);
            current_result_reg = saved;

            // Save V0 while computing address
            uint8_t saved_val_reg = allocate_register();
            emit_opcode(0x8000 | (saved_val_reg << 8) | (val_reg << 4) | 0x0); // LD saved_val, V0

            // Compute address offset
            uint8_t idx_reg = allocate_register();

            if (node.index) {
                // Array access: entity[i].field = value
                saved = current_result_reg;
                current_result_reg = idx_reg;
                node.index->accept(*this);
                current_result_reg = saved;

                // Multiply by entity size if > 1
                if (entity_size > 1) {
                    uint8_t size_reg = allocate_register();
                    emit_opcode(0x6000 | (size_reg << 8) | (entity_size & 0xFF)); // LD size_reg, entity_size

                    uint8_t temp_reg = allocate_register();
                    emit_opcode(0x8000 | (temp_reg << 8) | (idx_reg << 4) | 0x0); // LD temp, idx
                    emit_opcode(0x6000 | (idx_reg << 8) | 0x00); // LD idx, 0
                    uint16_t mult_loop = static_cast<uint16_t>(output.size());
                    emit_opcode(0x4000 | (size_reg << 8) | 0x00); // SNE size_reg, 0 - skip JP if size != 0
                    uint16_t mult_end = output.size();
                    emit_jump(0); // exit when size == 0
                    emit_opcode(0x8000 | (idx_reg << 8) | (temp_reg << 4) | 0x4); // ADD idx, temp
                    emit_opcode(0x7000 | (size_reg << 8) | 0xFF); // ADD size_reg, -1
                    emit_jump(mult_loop);
                    patch_jump_at(mult_end, static_cast<uint16_t>(output.size()));
                    free_register(temp_reg);
                    free_register(size_reg);
                }

                // Add field offset
                if (field_offset > 0) {
                    emit_opcode(0x7000 | (idx_reg << 8) | (field_offset & 0xFF)); // ADD idx_reg, field_offset
                }
            } else {
                // Single entity: just use field offset
                emit_opcode(0x6000 | (idx_reg << 8) | (field_offset & 0xFF)); // LD idx_reg, field_offset
            }

            // Restore value to V0
            emit_opcode(0x8000 | (val_reg << 8) | (saved_val_reg << 4) | 0x0); // LD V0, saved_val
            free_register(saved_val_reg);

            // Load I = base_addr
            emit_opcode(0xA000 | (base_addr & 0x0FFF)); // LD I, base_addr

            // Add index to I
            emit_opcode(0xF01E | (idx_reg << 8)); // ADD I, idx_reg

            // Store V0 at [I]
            emit_opcode(0xF055 | (0 << 8)); // LD [I], V0

            free_register(idx_reg);
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

            // Scan for patterns (instructions are 2 bytes each)
            for (size_t i = 0; i + 3 < output.size(); i += 2) {
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
            }
            
            // Also check last instruction for LD Vx, Vx
            if (output.size() >= 2) {
                size_t i = output.size() - 2;
                uint16_t op = (output[i] << 8) | output[i + 1];
                bool follows_skip = (i >= 2) && is_skip((output[i - 2] << 8) | output[i - 1]);
                if (follows_skip) op = 0; // never remove a skipped instruction
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
                }
            }
            
            output = std::move(new_output);
            return bytes_saved;
        }
    }
