#include <chip8/decompiler.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <queue>

namespace chip8 {

// CFGInstruction helpers
bool CFGInstruction::isSkip() const {
    uint16_t type = opcode & 0xF000;
    uint8_t low = opcode & 0x00FF;
    
    if (type == 0x3000) return true;  // SE Vx, byte
    if (type == 0x4000) return true;  // SNE Vx, byte
    if (type == 0x5000 && (opcode & 0xF) == 0) return true;  // SE Vx, Vy
    if (type == 0x9000 && (opcode & 0xF) == 0) return true;  // SNE Vx, Vy
    if (type == 0xE000 && (low == 0x9E || low == 0xA1)) return true;  // SKP/SKNP
    
    return false;
}

Decompiler::Decompiler() = default;

void Decompiler::analyze(const std::vector<uint8_t>& rom_data) {
    rom = rom_data;
    blocks.clear();
    functions.clear();
    loops.clear();
    branches.clear();
    patterns.clear();
    is_code.clear();
    register_names.clear();
    function_reach.clear();
    emitted_addresses.clear();
    processing_loop_headers.clear();
    sprites.clear();
    sprite_names.clear();
    
    if (rom.size() < 2) return;
    
    buildCFG();
    detectFunctions();
    computeFunctionReachability();
    detectLoops();
    detectIfElse();
    detectSpriteData();  // ROM-agnostic sprite detection
    matchPatterns();
    inferVariableNames();
}

CFGInstruction Decompiler::getInstruction(uint16_t addr) const {
    CFGInstruction instr;
    instr.address = addr;
    
    size_t offset = addr - 0x200;
    if (offset + 1 < rom.size()) {
        instr.opcode = (rom[offset] << 8) | rom[offset + 1];
    } else {
        instr.opcode = 0;
    }
    instr.mnemonic = disassembleOpcode(instr.opcode);
    return instr;
}

const PatternMatch* Decompiler::getPatternAt(uint16_t addr) const {
    for (const auto& p : patterns) {
        if (p.start_addr == addr) return &p;
    }
    return nullptr;
}

void Decompiler::buildCFG() {
    is_code.resize(rom.size(), false);
    
    std::set<uint16_t> visited;
    std::set<uint16_t> block_starts;
    std::vector<uint16_t> work_list;
    
    work_list.push_back(0x200);
    block_starts.insert(0x200);
    
    while (!work_list.empty()) {
        uint16_t addr = work_list.back();
        work_list.pop_back();
        
        size_t offset = addr - 0x200;
        if (offset >= rom.size() - 1 || visited.count(addr)) continue;
        visited.insert(addr);
        
        is_code[offset] = true;
        is_code[offset + 1] = true;
        
        CFGInstruction instr = getInstruction(addr);
        
        if (instr.isJump()) {
            uint16_t target = instr.nnn();
            if (target >= 0x200 && target - 0x200 < rom.size()) {
                work_list.push_back(target);
                block_starts.insert(target);
            }
        } else if (instr.isCall()) {
            uint16_t target = instr.nnn();
            if (target >= 0x200 && target - 0x200 < rom.size()) {
                work_list.push_back(target);
                block_starts.insert(target);
            }
            work_list.push_back(addr + 2);
            block_starts.insert(addr + 2);
        } else if (instr.isReturn()) {
            // Return ends block
        } else if (instr.isSkip()) {
            work_list.push_back(addr + 2);
            work_list.push_back(addr + 4);
            block_starts.insert(addr + 4);
        } else {
            work_list.push_back(addr + 2);
        }
    }
    
    identifyBasicBlocks();
    connectBlocks();
}

void Decompiler::identifyBasicBlocks() {
    std::set<uint16_t> boundaries;
    boundaries.insert(0x200);
    
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (!is_code[i]) continue;
        
        uint16_t addr = 0x200 + static_cast<uint16_t>(i);
        CFGInstruction instr = getInstruction(addr);
        
        if (instr.isJump() || instr.isReturn() || instr.isCall()) {
            if (i + 2 < rom.size()) {
                boundaries.insert(addr + 2);
            }
        }
        
        if (instr.isJump() || instr.isCall()) {
            uint16_t target = instr.nnn();
            if (target >= 0x200) {
                boundaries.insert(target);
            }
        }
        
        if (instr.isSkip()) {
            boundaries.insert(addr + 4);
        }
    }
    
    auto it = boundaries.begin();
    while (it != boundaries.end()) {
        uint16_t start = *it;
        ++it;
        
        uint16_t end = (it != boundaries.end()) ? *it : 0x200 + static_cast<uint16_t>(rom.size());
        
        if (start - 0x200 >= rom.size()) continue;
        
        BasicBlock block;
        block.start_addr = start;
        block.end_addr = end;
        
        for (uint16_t addr = start; addr < end && addr - 0x200 + 1 < rom.size(); addr += 2) {
            size_t offset = addr - 0x200;
            if (is_code[offset]) {
                block.instructions.push_back(getInstruction(addr));
            } else {
                block.is_data = true;
                break;
            }
            
            CFGInstruction& instr = block.instructions.back();
            if (instr.isJump() || instr.isReturn() || instr.isCall()) {
                block.end_addr = addr + 2;
                break;
            }
        }
        
        if (!block.instructions.empty()) {
            blocks[start] = block;
        }
    }
}

void Decompiler::connectBlocks() {
    for (auto& [addr, block] : blocks) {
        if (block.instructions.empty()) continue;
        
        CFGInstruction& last = block.instructions.back();
        
        if (last.isJump()) {
            uint16_t target = last.nnn();
            block.successors.push_back(target);
            if (blocks.count(target)) {
                blocks[target].predecessors.push_back(addr);
            }
        } else if (last.isCall()) {
            if (blocks.count(block.end_addr)) {
                block.successors.push_back(block.end_addr);
                blocks[block.end_addr].predecessors.push_back(addr);
            }
        } else if (last.isReturn()) {
            // No successors
        } else if (last.isSkip()) {
            uint16_t fall_through = block.end_addr;
            uint16_t skip_target = block.end_addr + 2;
            
            if (blocks.count(fall_through)) {
                block.successors.push_back(fall_through);
                blocks[fall_through].predecessors.push_back(addr);
            }
            if (blocks.count(skip_target)) {
                block.successors.push_back(skip_target);
                blocks[skip_target].predecessors.push_back(addr);
            }
        } else {
            if (blocks.count(block.end_addr)) {
                block.successors.push_back(block.end_addr);
                blocks[block.end_addr].predecessors.push_back(addr);
            }
        }
    }
}

void Decompiler::detectFunctions() {
    Function main_func;
    main_func.name = "main";
    main_func.entry_addr = 0x200;
    functions.push_back(main_func);
    
    if (blocks.count(0x200)) {
        blocks[0x200].is_function_entry = true;
    }
    
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (!is_code[i]) continue;
        
        uint16_t opcode = (rom[i] << 8) | rom[i + 1];
        if ((opcode & 0xF000) == 0x2000) {
            uint16_t target = opcode & 0x0FFF;
            
            bool found = false;
            for (const auto& func : functions) {
                if (func.entry_addr == target) {
                    found = true;
                    break;
                }
            }
            
            if (!found && target >= 0x200) {
                Function func;
                func.entry_addr = target;
                std::ostringstream ss;
                ss << "func_" << std::hex << std::uppercase << target;
                func.name = ss.str();
                functions.push_back(func);
                
                if (blocks.count(target)) {
                    blocks[target].is_function_entry = true;
                }
            }
        }
    }
    
    std::sort(functions.begin(), functions.end(),
              [](const Function& a, const Function& b) {
                  return a.entry_addr < b.entry_addr;
              });
}

void Decompiler::computeFunctionReachability() {
    // For each function, compute all reachable addresses via control flow
    // This handles cases where main() jumps past embedded functions
    
    std::set<uint16_t> function_entries;
    for (const auto& func : functions) {
        function_entries.insert(func.entry_addr);
    }
    
    for (const auto& func : functions) {
        FunctionReachability reach;
        std::set<uint16_t> visited;
        std::vector<uint16_t> work_list;
        work_list.push_back(func.entry_addr);
        
        while (!work_list.empty()) {
            uint16_t addr = work_list.back();
            work_list.pop_back();
            
            if (visited.count(addr)) continue;
            
            size_t offset = addr - 0x200;
            if (offset >= rom.size() - 1) continue;
            if (!is_code[offset]) continue;
            
            // Stop at other function entries (they have their own reachability)
            if (addr != func.entry_addr && function_entries.count(addr)) {
                continue;
            }
            
            visited.insert(addr);
            reach.reachable_addrs.insert(addr);
            if (addr < reach.min_addr) reach.min_addr = addr;
            if (addr > reach.max_addr) reach.max_addr = addr;
            
            CFGInstruction instr = getInstruction(addr);
            
            if (instr.isJump()) {
                uint16_t target = instr.nnn();
                if (target >= 0x200 && target - 0x200 < rom.size()) {
                    work_list.push_back(target);
                }
            } else if (instr.isCall()) {
                // Don't follow into the callee, but continue after the call
                work_list.push_back(addr + 2);
            } else if (instr.isReturn()) {
                // Return ends this path
            } else if (instr.isSkip()) {
                work_list.push_back(addr + 2);  // Fall through
                work_list.push_back(addr + 4);  // Skip target
            } else {
                work_list.push_back(addr + 2);
            }
        }
        
        function_reach[func.entry_addr] = reach;
    }
}

void Decompiler::detectLoops() {
    for (const auto& [addr, block] : blocks) {
        for (uint16_t succ : block.successors) {
            if (succ <= addr) {
                // Don't treat function entries as loop headers for external calls
                bool is_func_entry = false;
                for (const auto& func : functions) {
                    if (func.entry_addr == succ && func.entry_addr != 0x200) {
                        is_func_entry = true;
                        break;
                    }
                }
                if (is_func_entry) continue;
                
                // Check if the back edge is actually a jump, not a fall-through after call
                if (!block.instructions.empty()) {
                    const auto& last_instr = block.instructions.back();
                    // Only count as loop if it's an actual jump back
                    if (!last_instr.isJump()) continue;
                }
                
                LoopInfo loop;
                loop.header_addr = succ;
                loop.back_edge_addr = addr;
                
                if (succ == addr) {
                    loop.type = LoopInfo::LoopType::INFINITE;
                } else {
                    loop.type = LoopInfo::LoopType::WHILE;
                }
                
                loops.push_back(loop);
                
                if (blocks.count(succ)) {
                    blocks[succ].is_loop_header = true;
                }
            }
        }
    }
}

void Decompiler::detectIfElse() {
    for (size_t i = 0; i + 3 < rom.size(); i += 2) {
        if (!is_code[i]) continue;
        
        uint16_t addr = 0x200 + static_cast<uint16_t>(i);
        CFGInstruction skip_instr = getInstruction(addr);
        
        if (!skip_instr.isSkip()) continue;
        
        CFGInstruction next_instr = getInstruction(addr + 2);
        if (next_instr.isJump()) {
            BranchInfo branch;
            branch.condition_addr = addr;
            branch.else_addr = addr + 4;
            branch.then_addr = next_instr.nnn();
            branch.merge_addr = 0;
            branches.push_back(branch);
        }
    }
}

bool Decompiler::isLoopHeader(uint16_t addr) const {
    for (const auto& loop : loops) {
        if (loop.header_addr == addr) return true;
    }
    return false;
}

uint16_t Decompiler::getLoopEnd(uint16_t header_addr) const {
    for (const auto& loop : loops) {
        if (loop.header_addr == header_addr) {
            return loop.back_edge_addr + 2;
        }
    }
    return 0;
}

bool Decompiler::isIfPattern(uint16_t addr, uint16_t& then_addr, uint16_t& else_addr, uint16_t& end_addr) const {
    for (const auto& branch : branches) {
        if (branch.condition_addr == addr) {
            then_addr = branch.then_addr;
            else_addr = branch.else_addr;
            end_addr = branch.merge_addr;
            return true;
        }
    }
    return false;
}

void Decompiler::matchPatterns() {
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (!is_code[i]) continue;
        
        uint16_t addr = 0x200 + static_cast<uint16_t>(i);
        
        PatternMatch match;
        // Try longer patterns first
        if (matchKeyPressedIf(addr, match)) {
            patterns.push_back(match);
        } else if (matchBoolCompareIf(addr, match)) {
            patterns.push_back(match);
        } else if (matchWaitDelay(addr, match)) {
            patterns.push_back(match);
        } else if (matchKeyExpression(addr, match)) {
            patterns.push_back(match);
        } else if (matchEqualityCheck(addr, match)) {
            patterns.push_back(match);
        } else if (matchLessThan(addr, match)) {
            patterns.push_back(match);
        }
    }
}

bool Decompiler::matchWaitDelay(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 7 >= rom.size()) return false;
    
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    uint16_t op3 = (rom[offset + 4] << 8) | rom[offset + 5];
    uint16_t op4 = (rom[offset + 6] << 8) | rom[offset + 7];
    
    if ((op1 & 0xF0FF) != 0xF015) return false;
    uint8_t reg = (op1 >> 8) & 0xF;
    
    if ((op2 & 0xF0FF) != 0xF007) return false;
    if (((op2 >> 8) & 0xF) != reg) return false;
    
    if ((op3 & 0xF0FF) != 0x3000) return false;
    if (((op3 >> 8) & 0xF) != reg) return false;
    
    if ((op4 & 0xF000) != 0x1000) return false;
    if ((op4 & 0x0FFF) != addr + 2) return false;
    
    match.type = PatternMatch::Type::WAIT_DELAY;
    match.start_addr = addr;
    match.end_addr = addr + 8;
    match.params["reg"] = reg;
    return true;
}

bool Decompiler::matchKeyCheck(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 1 >= rom.size()) return false;
    
    uint16_t op = (rom[offset] << 8) | rom[offset + 1];
    
    if ((op & 0xF0FF) == 0xE09E || (op & 0xF0FF) == 0xE0A1) {
        match.type = PatternMatch::Type::KEY_CHECK;
        match.start_addr = addr;
        match.end_addr = addr + 2;
        match.params["reg"] = (op >> 8) & 0xF;
        match.params["pressed"] = ((op & 0xFF) == 0x9E) ? 1 : 0;
        return true;
    }
    return false;
}

bool Decompiler::matchKeyExpression(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 5 >= rom.size()) return false;
    
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    uint16_t op3 = (rom[offset + 4] << 8) | rom[offset + 5];
    
    // LD Vx, 1
    if ((op1 & 0xF0FF) != 0x6001) return false;
    uint8_t result_reg = (op1 >> 8) & 0xF;
    
    // SKP Vy or SKNP Vy
    if ((op2 & 0xF0FF) != 0xE09E && (op2 & 0xF0FF) != 0xE0A1) return false;
    uint8_t key_reg = (op2 >> 8) & 0xF;
    bool is_skp = (op2 & 0xFF) == 0x9E;
    
    // LD Vx, 0
    if ((op3 & 0xF0FF) != 0x6000) return false;
    if (((op3 >> 8) & 0xF) != result_reg) return false;
    
    match.type = PatternMatch::Type::KEY_CHECK;
    match.start_addr = addr;
    match.end_addr = addr + 6;
    match.params["result_reg"] = result_reg;
    match.params["key_reg"] = key_reg;
    // SKP means: skip if pressed, so result=1 when pressed (not inverted)
    // SKNP means: skip if NOT pressed, so result=1 when NOT pressed (inverted)
    match.params["inverted"] = is_skp ? 0 : 1;
    return true;
}

// Match SCL equality check: x ^= y; SE x, 0 -> (x == y)
bool Decompiler::matchEqualityCheck(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 3 >= rom.size()) return false;
    
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    
    // XOR Vx, Vy
    if ((op1 & 0xF00F) != 0x8003) return false;
    uint8_t x = (op1 >> 8) & 0xF;
    uint8_t y = (op1 >> 4) & 0xF;
    
    // SE Vx, 0 or SNE Vx, 0
    if ((op2 & 0xF0FF) == 0x3000 && ((op2 >> 8) & 0xF) == x) {
        match.type = PatternMatch::Type::EQUALITY_CHECK;
        match.start_addr = addr;
        match.end_addr = addr + 4;
        match.params["x"] = x;
        match.params["y"] = y;
        match.params["equal"] = 1;  // checking if equal
        return true;
    }
    if ((op2 & 0xF0FF) == 0x4000 && ((op2 >> 8) & 0xF) == x) {
        match.type = PatternMatch::Type::EQUALITY_CHECK;
        match.start_addr = addr;
        match.end_addr = addr + 4;
        match.params["x"] = x;
        match.params["y"] = y;
        match.params["equal"] = 0;  // checking if not equal
        return true;
    }
    
    return false;
}

// Match SCL less-than: x -= y; result = 1; result -= VF -> (original_x < original_y)
bool Decompiler::matchLessThan(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 5 >= rom.size()) return false;
    
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    uint16_t op3 = (rom[offset + 4] << 8) | rom[offset + 5];
    
    // SUB Vx, Vy (8xy5)
    if ((op1 & 0xF00F) != 0x8005) return false;
    uint8_t x = (op1 >> 8) & 0xF;
    uint8_t y = (op1 >> 4) & 0xF;
    
    // LD Vr, 1
    if ((op2 & 0xF0FF) != 0x6001) return false;
    uint8_t result = (op2 >> 8) & 0xF;
    
    // SUB Vr, VF (8rF5) - result -= VF
    if ((op3 & 0xF00F) != 0x8005) return false;  // Must be SUB instruction
    if (((op3 >> 8) & 0xF) != result) return false;  // Vx must be result
    if (((op3 >> 4) & 0xF) != 0xF) return false;  // Vy must be VF (0xF)
    
    match.type = PatternMatch::Type::LESS_THAN;
    match.start_addr = addr;
    match.end_addr = addr + 6;
    match.params["x"] = x;
    match.params["y"] = y;
    match.params["result"] = result;
    return true;
}

// Match full key pressed if-block pattern:
// LD Vk, key_num; [key expr]; LD Vy, 0; LD Vf, 0; SNE Vr, Vy; LD Vf, 1; SNE Vf, 0; JP target
// This matches the SCL boolean evaluation pattern for key checks
bool Decompiler::matchKeyPressedIf(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 17 >= rom.size()) return false;
    
    // op1: LD Vk, key_num (6knn)
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    if ((op1 & 0xF000) != 0x6000) return false;
    uint8_t key_reg = (op1 >> 8) & 0xF;
    uint8_t key_num = op1 & 0xFF;
    
    // op2-op4: KEY_CHECK pattern (LD Vr, 1; SKP/SKNP Vk; LD Vr, 0)
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    uint16_t op3 = (rom[offset + 4] << 8) | rom[offset + 5];
    uint16_t op4 = (rom[offset + 6] << 8) | rom[offset + 7];
    
    if ((op2 & 0xF0FF) != 0x6001) return false;  // LD Vr, 1
    uint8_t result_reg = (op2 >> 8) & 0xF;
    
    if ((op3 & 0xF0FF) != 0xE09E && (op3 & 0xF0FF) != 0xE0A1) return false;  // SKP/SKNP Vk
    if (((op3 >> 8) & 0xF) != key_reg) return false;
    bool is_skp = (op3 & 0xFF) == 0x9E;  // SKP = result is 1 if pressed
    
    if ((op4 & 0xF0FF) != 0x6000) return false;  // LD Vr, 0
    if (((op4 >> 8) & 0xF) != result_reg) return false;
    
    // op5: LD Vy, 0 (6y00)
    uint16_t op5 = (rom[offset + 8] << 8) | rom[offset + 9];
    if ((op5 & 0xF0FF) != 0x6000) return false;
    uint8_t y_reg = (op5 >> 8) & 0xF;
    
    // op6: LD Vf, 0 (6f00) - flag register
    uint16_t op6 = (rom[offset + 10] << 8) | rom[offset + 11];
    if ((op6 & 0xF0FF) != 0x6000) return false;
    uint8_t flag_reg = (op6 >> 8) & 0xF;
    
    // op7: SNE Vr, Vy (9ry0) or SE Vr, Vy (5ry0) - skip if result != 0 (or == 0)
    uint16_t op7 = (rom[offset + 12] << 8) | rom[offset + 13];
    bool is_sne = (op7 & 0xF00F) == 0x9000;
    bool is_se = (op7 & 0xF00F) == 0x5000;
    if (!is_sne && !is_se) return false;
    if (((op7 >> 8) & 0xF) != result_reg) return false;
    if (((op7 >> 4) & 0xF) != y_reg) return false;
    
    // op8: LD Vf, 1 (6f01)
    uint16_t op8 = (rom[offset + 14] << 8) | rom[offset + 15];
    if ((op8 & 0xF0FF) != 0x6001) return false;
    if (((op8 >> 8) & 0xF) != flag_reg) return false;
    
    // op9: SNE Vf, 0 (4f00)
    uint16_t op9 = (rom[offset + 16] << 8) | rom[offset + 17];
    if ((op9 & 0xF0FF) != 0x4000) return false;
    if (((op9 >> 8) & 0xF) != flag_reg) return false;
    
    // op10: JP target (1nnn)
    if (offset + 19 >= rom.size()) return false;
    uint16_t op10 = (rom[offset + 18] << 8) | rom[offset + 19];
    if ((op10 & 0xF000) != 0x1000) return false;
    uint16_t jump_target = op10 & 0x0FFF;
    
    // Determine if this is key_pressed or !key_pressed
    // SKP + SNE: result=1 if pressed, SNE skips if result!=0 (pressed), flag stays 0, body skipped
    //            result=0 if not pressed, SNE doesn't skip, flag=1, body executes
    //            So body executes when NOT pressed -> if (!key_pressed(k))
    // SKP + SE:  result=1 if pressed, SE skips if result==0 (not), flag stays 0 when pressed
    //            result=0 if not pressed, SE skips, flag stays 0
    //            Actually SE Vr, Vy where y=0: skip if result==0
    //            If pressed (result=1): don't skip, flag=1, body executes -> if (key_pressed(k))
    bool check_pressed = (is_skp && is_se) || (!is_skp && is_sne);
    
    match.type = PatternMatch::Type::KEY_PRESSED_IF;
    match.start_addr = addr;
    match.end_addr = addr + 20;
    match.params["key_num"] = key_num;
    match.params["jump_target"] = jump_target;
    match.params["check_pressed"] = check_pressed ? 1 : 0;
    return true;
}

// Match boolean comparison if-block pattern:
// LD Vf, 1; SE/SNE Vx, Vy; LD Vf, 0; SNE Vf, 0; JP target
// This is used by SCL to evaluate (x == y) or (x != y) in if conditions
bool Decompiler::matchBoolCompareIf(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 9 >= rom.size()) return false;
    
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    uint16_t op3 = (rom[offset + 4] << 8) | rom[offset + 5];
    uint16_t op4 = (rom[offset + 6] << 8) | rom[offset + 7];
    uint16_t op5 = (rom[offset + 8] << 8) | rom[offset + 9];
    
    // Check for pattern starting with flag = 1
    // LD Vf, 1; SE/SNE Vx, Vy; LD Vf, 0; SNE Vf, 0; JP target
    if ((op1 & 0xF0FF) == 0x6001) {
        uint8_t flag_reg = (op1 >> 8) & 0xF;
        
        bool is_se = (op2 & 0xF00F) == 0x5000;
        bool is_sne = (op2 & 0xF00F) == 0x9000;
        if (!is_se && !is_sne) return false;
        uint8_t x_reg = (op2 >> 8) & 0xF;
        uint8_t y_reg = (op2 >> 4) & 0xF;
        
        if ((op3 & 0xF0FF) != 0x6000 || ((op3 >> 8) & 0xF) != flag_reg) return false;
        if ((op4 & 0xF0FF) != 0x4000 || ((op4 >> 8) & 0xF) != flag_reg) return false;
        if ((op5 & 0xF000) != 0x1000) return false;
        
        // SE: flag=1 when equal, 0 when not equal; SNE flag,0 skips when !=0 -> body when not equal
        // SNE: flag=1 when not equal, 0 when equal; SNE flag,0 skips when !=0 -> body when equal
        bool check_equal = is_sne;
        
        match.type = PatternMatch::Type::BOOL_COMPARE_IF;
        match.start_addr = addr;
        match.end_addr = addr + 10;
        match.params["x_reg"] = x_reg;
        match.params["y_reg"] = y_reg;
        match.params["jump_target"] = op5 & 0x0FFF;
        match.params["check_equal"] = check_equal ? 1 : 0;
        return true;
    }
    
    // Check for inverted pattern starting with flag = 0
    // LD Vf, 0; SE/SNE Vx, Vy; LD Vf, 1; SNE Vf, 0; JP target
    if ((op1 & 0xF0FF) == 0x6000) {
        uint8_t flag_reg = (op1 >> 8) & 0xF;
        
        bool is_se = (op2 & 0xF00F) == 0x5000;
        bool is_sne = (op2 & 0xF00F) == 0x9000;
        if (!is_se && !is_sne) return false;
        uint8_t x_reg = (op2 >> 8) & 0xF;
        uint8_t y_reg = (op2 >> 4) & 0xF;
        
        if ((op3 & 0xF0FF) != 0x6001 || ((op3 >> 8) & 0xF) != flag_reg) return false;
        if ((op4 & 0xF0FF) != 0x4000 || ((op4 >> 8) & 0xF) != flag_reg) return false;
        if ((op5 & 0xF000) != 0x1000) return false;
        
        // With flag starting at 0:
        // SE: flag becomes 1 when NOT equal; SNE flag,0 skips when !=0 -> body when equal
        // SNE: flag becomes 1 when equal; SNE flag,0 skips when !=0 -> body when not equal
        bool check_equal = is_se;
        
        match.type = PatternMatch::Type::BOOL_COMPARE_IF;
        match.start_addr = addr;
        match.end_addr = addr + 10;
        match.params["x_reg"] = x_reg;
        match.params["y_reg"] = y_reg;
        match.params["jump_target"] = op5 & 0x0FFF;
        match.params["check_equal"] = check_equal ? 1 : 0;
        return true;
    }
    
    return false;
}

void Decompiler::inferVariableNames() {
    register_names.clear();
    
    std::map<uint8_t, int> draw_x_count, draw_y_count;
    std::map<uint8_t, int> key_count;
    
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (!is_code[i]) continue;
        
        uint16_t opcode = (rom[i] << 8) | rom[i + 1];
        uint8_t x = (opcode >> 8) & 0xF;
        uint8_t y = (opcode >> 4) & 0xF;
        
        if ((opcode & 0xF000) == 0xD000) {
            draw_x_count[x]++;
            draw_y_count[y]++;
        }
        
        if ((opcode & 0xF0FF) == 0xE09E || (opcode & 0xF0FF) == 0xE0A1) {
            key_count[x]++;
        }
    }
    
    // Track which names are already used
    std::set<std::string> used_names;
    
    // Find the register most used as x-coordinate
    int best_x_count = 0;
    uint8_t best_x_reg = 0xFF;
    for (auto& [reg, count] : draw_x_count) {
        if (count > best_x_count && draw_y_count[reg] < count) {
            best_x_count = count;
            best_x_reg = reg;
        }
    }
    if (best_x_reg != 0xFF && best_x_count >= 2) {
        register_names[best_x_reg] = "x";
        used_names.insert("x");
    }
    
    // Find the register most used as y-coordinate  
    int best_y_count = 0;
    uint8_t best_y_reg = 0xFF;
    for (auto& [reg, count] : draw_y_count) {
        if (count > best_y_count && draw_x_count[reg] < count && !register_names.count(reg)) {
            best_y_count = count;
            best_y_reg = reg;
        }
    }
    if (best_y_reg != 0xFF && best_y_count >= 2) {
        register_names[best_y_reg] = "y";
        used_names.insert("y");
    }
}

std::string Decompiler::indent_str(int level) const {
    return std::string(level * 4, ' ');
}

std::string Decompiler::regName(uint8_t reg) const {
    if (use_variable_names && register_names.count(reg)) {
        return register_names.at(reg);
    }
    std::ostringstream ss;
    ss << "V" << std::hex << std::uppercase << (int)reg;
    return ss.str();
}

std::string Decompiler::generateExpression(const CFGInstruction& instr) {
    std::ostringstream ss;
    uint16_t op = instr.opcode;
    uint8_t x = instr.x();
    uint8_t y = instr.y();
    uint8_t n = instr.n();
    uint8_t nn = instr.nn();
    uint16_t nnn = instr.nnn();
    
    switch (op & 0xF000) {
        case 0x0000:
            if (op == 0x00E0) return "clear()";
            if (op == 0x00EE) return "return";
            break;
            
        case 0x1000:
            for (const auto& loop : loops) {
                if (loop.back_edge_addr == instr.address && loop.header_addr == nnn) {
                    return "";
                }
            }
            ss << "goto L" << std::hex << std::uppercase << nnn;
            return ss.str();
            
        case 0x2000:
            for (const auto& func : functions) {
                if (func.entry_addr == nnn) {
                    ss << func.name << "()";
                    return ss.str();
                }
            }
            ss << "func_" << std::hex << std::uppercase << nnn << "()";
            return ss.str();
            
        case 0x3000:
            ss << "if (" << regName(x) << " == " << (int)nn << ")";
            return ss.str();
            
        case 0x4000:
            ss << "if (" << regName(x) << " != " << (int)nn << ")";
            return ss.str();
            
        case 0x5000:
            ss << "if (" << regName(x) << " == " << regName(y) << ")";
            return ss.str();
            
        case 0x6000:
            ss << regName(x) << " = " << (int)nn;
            return ss.str();
            
        case 0x7000:
            if (nn < 128) {
                ss << regName(x) << " += " << (int)nn;
            } else {
                ss << regName(x) << " -= " << (256 - nn);
            }
            return ss.str();
            
        case 0x8000:
            switch (n) {
                case 0x0: ss << regName(x) << " = " << regName(y); break;
                case 0x1: ss << regName(x) << " |= " << regName(y); break;
                case 0x2: ss << regName(x) << " &= " << regName(y); break;
                case 0x3: ss << regName(x) << " ^= " << regName(y); break;
                case 0x4: ss << regName(x) << " += " << regName(y); break;
                case 0x5: ss << regName(x) << " -= " << regName(y); break;
                case 0x6: ss << regName(x) << " >>= 1"; break;
                case 0x7: ss << regName(x) << " = " << regName(y) << " - " << regName(x); break;
                case 0xE: ss << regName(x) << " <<= 1"; break;
            }
            return ss.str();
            
        case 0x9000:
            ss << "if (" << regName(x) << " != " << regName(y) << ")";
            return ss.str();
            
        case 0xA000:
            ss << "I = 0x" << std::hex << std::uppercase << nnn;
            return ss.str();
            
        case 0xB000:
            ss << "goto 0x" << std::hex << std::uppercase << nnn << " + V0";
            return ss.str();
            
        case 0xC000:
            ss << regName(x) << " = rand(" << (int)nn << ")";
            return ss.str();
            
        case 0xD000:
            ss << "draw(" << regName(x) << ", " << regName(y) << ", " << (int)n << ")";
            return ss.str();
            
        case 0xE000:
            if (nn == 0x9E) {
                ss << "if (key(" << regName(x) << "))";
            } else if (nn == 0xA1) {
                ss << "if (!key(" << regName(x) << "))";
            }
            return ss.str();
            
        case 0xF000:
            switch (nn) {
                case 0x07: ss << regName(x) << " = timer()"; break;
                case 0x0A: ss << regName(x) << " = waitkey()"; break;
                case 0x15: ss << "delay(" << regName(x) << ")"; break;
                case 0x18: ss << "beep(" << regName(x) << ")"; break;
                case 0x1E: ss << "I += " << regName(x); break;
                case 0x29: ss << "I = font(" << regName(x) << ")"; break;
                case 0x33: ss << "bcd(" << regName(x) << ")"; break;
                case 0x55: ss << "store(V0.." << regName(x) << ")"; break;
                case 0x65: ss << "load(V0.." << regName(x) << ")"; break;
            }
            return ss.str();
    }
    
    ss << "/* 0x" << std::hex << std::uppercase << op << " */";
    return ss.str();
}

std::string Decompiler::generateStatement(const CFGInstruction& instr, int indent) {
    std::string expr = generateExpression(instr);
    if (expr.empty()) return "";
    
    std::ostringstream ss;
    ss << indent_str(indent);
    if (show_addresses) {
        ss << "/* " << std::hex << std::uppercase << std::setfill('0') 
           << std::setw(3) << instr.address << " */ ";
    }
    ss << expr;
    return ss.str();
}

std::string Decompiler::generateStructuredCode(uint16_t start_addr, uint16_t end_addr, int indent) {
    std::ostringstream ss;
    uint16_t addr = start_addr;
    
    while (addr < end_addr && addr - 0x200 + 1 < rom.size()) {
        if (emitted_addresses.count(addr)) {
            addr += 2;
            continue;
        }
        
        // Skip non-code addresses (sprite/data regions)
        size_t offset = addr - 0x200;
        if (offset < is_code.size() && !is_code[offset]) {
            addr += 2;
            continue;
        }
        
        const PatternMatch* pattern = getPatternAt(addr);
        if (pattern && pattern->type == PatternMatch::Type::WAIT_DELAY) {
            ss << indent_str(indent) << "wait()\n";
            emitted_addresses.insert(addr);
            emitted_addresses.insert(addr + 2);
            emitted_addresses.insert(addr + 4);
            emitted_addresses.insert(addr + 6);
            addr = pattern->end_addr;
            continue;
        }
        
        // Handle KEY_PRESSED_IF pattern - fold entire key check + boolean logic into if (key_pressed(n))
        if (pattern && pattern->type == PatternMatch::Type::KEY_PRESSED_IF) {
            int key_num = pattern->params.at("key_num");
            uint16_t jump_target = pattern->params.at("jump_target");
            bool check_pressed = pattern->params.at("check_pressed");
            
            // Mark all instructions in the pattern as emitted
            for (uint16_t a = addr; a < pattern->end_addr; a += 2) {
                emitted_addresses.insert(a);
            }
            
            uint16_t body_start = pattern->end_addr;
            if (jump_target > body_start && jump_target <= end_addr) {
                ss << indent_str(indent) << "if (";
                if (!check_pressed) ss << "!";
                ss << "key_pressed(" << key_num << ")) {\n";
                ss << generateStructuredCode(body_start, jump_target, indent + 1);
                ss << indent_str(indent) << "}\n";
                addr = jump_target;
            } else {
                ss << indent_str(indent) << "if (";
                if (!check_pressed) ss << "!";
                ss << "key_pressed(" << key_num << "))\n";
                addr = pattern->end_addr;
            }
            continue;
        }
        
        // Handle BOOL_COMPARE_IF pattern - fold Vf=1; SE/SNE x,y; Vf=0; SNE Vf,0; JP into if (x == y) or if (x != y)
        if (pattern && pattern->type == PatternMatch::Type::BOOL_COMPARE_IF) {
            uint8_t x_reg = pattern->params.at("x_reg");
            uint8_t y_reg = pattern->params.at("y_reg");
            uint16_t jump_target = pattern->params.at("jump_target");
            bool check_equal = pattern->params.at("check_equal");
            
            // Mark all instructions in the pattern as emitted
            for (uint16_t a = addr; a < pattern->end_addr; a += 2) {
                emitted_addresses.insert(a);
            }
            
            uint16_t body_start = pattern->end_addr;
            if (jump_target > body_start && jump_target <= end_addr) {
                ss << indent_str(indent) << "if (" << regName(x_reg);
                ss << (check_equal ? " == " : " != ") << regName(y_reg) << ") {\n";
                ss << generateStructuredCode(body_start, jump_target, indent + 1);
                ss << indent_str(indent) << "}\n";
                addr = jump_target;
            } else {
                ss << indent_str(indent) << "if (" << regName(x_reg);
                ss << (check_equal ? " == " : " != ") << regName(y_reg) << ")\n";
                addr = pattern->end_addr;
            }
            continue;
        }
        
        if (pattern && pattern->type == PatternMatch::Type::KEY_CHECK && 
            pattern->params.count("result_reg")) {
            uint8_t result = pattern->params.at("result_reg");
            uint8_t key = pattern->params.at("key_reg");
            bool inverted = pattern->params.at("inverted");
            ss << indent_str(indent) << regName(result) << " = ";
            if (inverted) ss << "!";
            ss << "key(" << regName(key) << ")\n";
            emitted_addresses.insert(addr);
            emitted_addresses.insert(addr + 2);
            emitted_addresses.insert(addr + 4);
            addr = pattern->end_addr;
            continue;
        }
        
        // Handle equality check pattern: x ^= y; SE/SNE x, 0
        if (pattern && pattern->type == PatternMatch::Type::EQUALITY_CHECK) {
            uint8_t x = pattern->params.at("x");
            uint8_t y = pattern->params.at("y");
            bool equal = pattern->params.at("equal");
            
            // XOR with self is always 0, so (x == x) is always true, (x != x) always false
            bool is_self_xor = (x == y);
            bool always_true = is_self_xor && equal;
            bool always_false = is_self_xor && !equal;
            
            if (always_false) {
                // Skip this entire pattern and its body - it's dead code
                emitted_addresses.insert(addr);
                emitted_addresses.insert(addr + 2);
                addr = pattern->end_addr;
                continue;
            }
            
            if (!always_true) {
                ss << indent_str(indent) << "if (" << regName(x);
                ss << (equal ? " == " : " != ") << regName(y) << ")";
            }
            emitted_addresses.insert(addr);
            emitted_addresses.insert(addr + 2);
            
            // Check if followed by jump for if-block
            uint16_t next_addr = pattern->end_addr;
            if (next_addr - 0x200 + 1 < rom.size()) {
                CFGInstruction next = getInstruction(next_addr);
                if (next.isJump()) {
                    emitted_addresses.insert(next_addr);
                    uint16_t jump_target = next.nnn();
                    uint16_t skip_target = next_addr + 2;
                    
                    if (jump_target > skip_target && jump_target <= end_addr) {
                        if (always_true) {
                            // Just emit the body directly, no if
                            ss << generateStructuredCode(skip_target, jump_target, indent);
                        } else {
                            ss << " {\n";
                            ss << generateStructuredCode(skip_target, jump_target, indent + 1);
                            ss << indent_str(indent) << "}\n";
                        }
                        addr = jump_target;
                        continue;
                    }
                } else {
                    // Skip followed by non-jump - next instruction is the body
                    emitted_addresses.insert(next_addr);
                    std::string body = generateExpression(next);
                    if (always_true) {
                        ss << indent_str(indent) << body << "\n";
                    } else {
                        ss << " " << body << "\n";
                    }
                    addr = next_addr + 2;
                    continue;
                }
            }
            if (!always_true) ss << "\n";
            addr = pattern->end_addr;
            continue;
        }
        
        // Handle less-than pattern: x -= y; result = 1; result -= VF
        if (pattern && pattern->type == PatternMatch::Type::LESS_THAN) {
            uint8_t x_reg = pattern->params.at("x");
            uint8_t y_reg = pattern->params.at("y");
            uint8_t result = pattern->params.at("result");
            
            emitted_addresses.insert(addr);
            emitted_addresses.insert(addr + 2);
            emitted_addresses.insert(addr + 4);
            
            // x < x is always false - skip any following if-block
            if (x_reg == y_reg) {
                // Check if followed by SNE result, 0 and JP - skip the dead code
                uint16_t check_addr = pattern->end_addr;
                if (check_addr - 0x200 + 3 < rom.size()) {
                    uint16_t op1 = (rom[check_addr - 0x200] << 8) | rom[check_addr - 0x200 + 1];
                    // SNE Vresult, 0 (4r00)
                    if ((op1 & 0xF0FF) == 0x4000 && ((op1 >> 8) & 0xF) == result) {
                        uint16_t op2 = (rom[check_addr - 0x200 + 2] << 8) | rom[check_addr - 0x200 + 3];
                        // JP target
                        if ((op2 & 0xF000) == 0x1000) {
                            // Skip the entire dead if-block
                            emitted_addresses.insert(check_addr);
                            emitted_addresses.insert(check_addr + 2);
                            addr = op2 & 0x0FFF;  // Jump to target
                            continue;
                        }
                    }
                }
                ss << indent_str(indent) << "// (x < x) is always false - dead code follows\n";
                addr = pattern->end_addr;
                continue;
            }
            
            // Check if followed by SNE result, 0 and JP - fold into if (x < y)
            uint16_t check_addr = pattern->end_addr;
            if (check_addr - 0x200 + 3 < rom.size()) {
                uint16_t op1 = (rom[check_addr - 0x200] << 8) | rom[check_addr - 0x200 + 1];
                // SNE Vresult, 0 (4r00)
                if ((op1 & 0xF0FF) == 0x4000 && ((op1 >> 8) & 0xF) == result) {
                    uint16_t op2 = (rom[check_addr - 0x200 + 2] << 8) | rom[check_addr - 0x200 + 3];
                    // JP target
                    if ((op2 & 0xF000) == 0x1000) {
                        uint16_t jump_target = op2 & 0x0FFF;
                        uint16_t skip_target = check_addr + 4;
                        
                        emitted_addresses.insert(check_addr);
                        emitted_addresses.insert(check_addr + 2);
                        
                        if (jump_target > skip_target && jump_target <= end_addr) {
                            // Fold into: if (x < y) { ... }
                            ss << indent_str(indent) << "if (" << regName(x_reg) << " < " << regName(y_reg) << ") {\n";
                            ss << generateStructuredCode(skip_target, jump_target, indent + 1);
                            ss << indent_str(indent) << "}\n";
                            addr = jump_target;
                            continue;
                        }
                    }
                }
            }
            
            // No folding possible - just output the assignment
            ss << indent_str(indent) << regName(result) << " = (" << regName(x_reg) << " < " << regName(y_reg) << ")\n";
            addr = pattern->end_addr;
            continue;
        }
        
        // Check for loop header BEFORE inserting into emitted_addresses
        // But skip if we're already processing this loop header (prevents infinite recursion)
        if (isLoopHeader(addr) && !processing_loop_headers.count(addr)) {
            uint16_t loop_end = getLoopEnd(addr);
            if (loop_end > addr) {
                ss << indent_str(indent) << "while (1) {\n";
                // Mark this loop header as being processed
                processing_loop_headers.insert(addr);
                ss << generateStructuredCode(addr, loop_end - 2, indent + 1);
                processing_loop_headers.erase(addr);
                ss << indent_str(indent) << "}\n";
                
                addr = loop_end;
                continue;
            }
        }
        
        CFGInstruction instr = getInstruction(addr);
        emitted_addresses.insert(addr);
        
        if (instr.isSkip() && addr + 2 - 0x200 + 1 < rom.size()) {
            CFGInstruction next = getInstruction(addr + 2);
            if (next.isJump()) {
                emitted_addresses.insert(addr + 2);
                
                uint16_t jump_target = next.nnn();
                uint16_t skip_target = addr + 4;
                
                std::string condition = generateExpression(instr);
                
                bool is_loop_back = false;
                for (const auto& loop : loops) {
                    if (jump_target == loop.header_addr && addr >= loop.header_addr) {
                        is_loop_back = true;
                        break;
                    }
                }
                
                if (is_loop_back) {
                    // Conditional continue: if condition is true, execute body; else jump to loop start
                    // The body is everything from skip_target to end_addr
                    ss << indent_str(indent) << condition << " {\n";
                    ss << generateStructuredCode(skip_target, end_addr, indent + 1);
                    ss << indent_str(indent) << "}\n";
                    addr = end_addr;
                    continue;
                } else if (jump_target > skip_target && jump_target <= end_addr) {
                    ss << indent_str(indent) << condition << " {\n";
                    ss << generateStructuredCode(skip_target, jump_target, indent + 1);
                    ss << indent_str(indent) << "}\n";
                    addr = jump_target;
                    continue;
                } else if (jump_target < skip_target) {
                    // Backward jump not detected as loop - might be a continue or complex control flow
                    // Treat as conditional continue: body is from skip_target to end_addr
                    ss << indent_str(indent) << condition << " {\n";
                    ss << generateStructuredCode(skip_target, end_addr, indent + 1);
                    ss << indent_str(indent) << "}\n";
                    addr = end_addr;
                    continue;
                } else {
                    // Forward jump beyond end_addr - unusual, just emit the condition
                    ss << indent_str(indent) << condition << " {\n";
                    ss << indent_str(indent + 1) << "goto L" << std::hex << std::uppercase << jump_target << "\n";
                    ss << indent_str(indent) << "}\n";
                    addr = skip_target;
                    continue;
                }
            } else {
                // Skip not followed by jump - the skipped instruction is the "then" body
                // Format as: if (cond) { body }
                std::string condition = generateExpression(instr);
                std::string body = generateExpression(next);
                emitted_addresses.insert(addr + 2);
                ss << indent_str(indent) << condition << " {\n";
                ss << indent_str(indent + 1) << body << "\n";
                ss << indent_str(indent) << "}\n";
                addr += 4;
                continue;
            }
        }
        
        std::string stmt = generateStatement(instr, indent);
        if (!stmt.empty()) {
            ss << stmt << "\n";
        }
        
        if (instr.isReturn()) {
            break;
        }
        if (instr.isJump()) {
            bool is_back_edge = false;
            for (const auto& loop : loops) {
                if (loop.back_edge_addr == addr) {
                    is_back_edge = true;
                    break;
                }
            }
            if (!is_back_edge) {
                break;
            }
        }
        
        addr += 2;
    }
    
    return ss.str();
}

std::string Decompiler::decompile() {
    std::ostringstream ss;
    
    ss << "// CHIP-8 Decompiled Code\n";
    ss << "// Generated by SCL 2.0 Decompiler\n\n";
    
    for (const auto& func : functions) {
        emitted_addresses.clear();
        processing_loop_headers.clear();
        
        ss << "void " << func.name << "() {\n";
        
        // Use reachability-based boundaries
        if (function_reach.count(func.entry_addr)) {
            const auto& reach = function_reach.at(func.entry_addr);
            uint16_t end = reach.max_addr + 2;
            
            // First pass: generate from entry point
            ss << generateStructuredCode(func.entry_addr, end, 1);
            
            // Second pass: generate any remaining reachable addresses with labels
            // This handles non-contiguous regions (e.g., main() jumps past func_202)
            std::vector<uint16_t> sorted_addrs(reach.reachable_addrs.begin(), reach.reachable_addrs.end());
            std::sort(sorted_addrs.begin(), sorted_addrs.end());
            
            for (uint16_t addr : sorted_addrs) {
                if (!emitted_addresses.count(addr)) {
                    // Found unemitted reachable code - emit label and generate
                    ss << "L" << std::hex << std::uppercase << addr << ":\n";
                    
                    // Find end of this contiguous region
                    uint16_t region_end = addr + 2;
                    while (region_end <= end && reach.reachable_addrs.count(region_end) && !emitted_addresses.count(region_end)) {
                        region_end += 2;
                    }
                    
                    ss << generateStructuredCode(addr, region_end, 1);
                }
            }
        } else {
            // Fallback to old behavior if reachability not computed
            uint16_t start = func.entry_addr;
            uint16_t end = 0x200 + static_cast<uint16_t>(rom.size());
            for (const auto& other : functions) {
                if (other.entry_addr > start && other.entry_addr < end) {
                    end = other.entry_addr;
                }
            }
            ss << generateStructuredCode(start, end, 1);
        }
        
        ss << "}\n\n";
    }
    
    return ss.str();
}

std::string Decompiler::decompileFunction(uint16_t entry_addr) {
    const Function* func = nullptr;
    for (const auto& f : functions) {
        if (f.entry_addr == entry_addr) {
            func = &f;
            break;
        }
    }
    
    if (!func) {
        return "// Function not found\n";
    }
    
    emitted_addresses.clear();
    
    std::ostringstream ss;
    ss << "void " << func->name << "() {\n";
    
    uint16_t end = 0x200 + static_cast<uint16_t>(rom.size());
    for (const auto& other : functions) {
        if (other.entry_addr > entry_addr && other.entry_addr < end) {
            end = other.entry_addr;
        }
    }
    
    ss << generateStructuredCode(entry_addr, end, 1);
    ss << "}\n";
    return ss.str();
}

std::string Decompiler::decompileRange(uint16_t start_addr, uint16_t end_addr) {
    emitted_addresses.clear();
    return generateStructuredCode(start_addr, end_addr, 0);
}

std::string Decompiler::disassembleOpcode(uint16_t opcode) {
    std::ostringstream ss;
    
    uint8_t x = (opcode >> 8) & 0xF;
    uint8_t y = (opcode >> 4) & 0xF;
    uint8_t n = opcode & 0xF;
    uint8_t nn = opcode & 0xFF;
    uint16_t nnn = opcode & 0xFFF;
    
    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) return "CLS";
            if (opcode == 0x00EE) return "RET";
            ss << "SYS " << std::hex << nnn;
            break;
        case 0x1000: ss << "JP " << std::hex << nnn; break;
        case 0x2000: ss << "CALL " << std::hex << nnn; break;
        case 0x3000: ss << "SE V" << std::hex << (int)x << ", " << (int)nn; break;
        case 0x4000: ss << "SNE V" << std::hex << (int)x << ", " << (int)nn; break;
        case 0x5000: ss << "SE V" << std::hex << (int)x << ", V" << (int)y; break;
        case 0x6000: ss << "LD V" << std::hex << (int)x << ", " << (int)nn; break;
        case 0x7000: ss << "ADD V" << std::hex << (int)x << ", " << (int)nn; break;
        case 0x8000:
            switch (n) {
                case 0: ss << "LD V" << std::hex << (int)x << ", V" << (int)y; break;
                case 1: ss << "OR V" << std::hex << (int)x << ", V" << (int)y; break;
                case 2: ss << "AND V" << std::hex << (int)x << ", V" << (int)y; break;
                case 3: ss << "XOR V" << std::hex << (int)x << ", V" << (int)y; break;
                case 4: ss << "ADD V" << std::hex << (int)x << ", V" << (int)y; break;
                case 5: ss << "SUB V" << std::hex << (int)x << ", V" << (int)y; break;
                case 6: ss << "SHR V" << std::hex << (int)x; break;
                case 7: ss << "SUBN V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0xE: ss << "SHL V" << std::hex << (int)x; break;
            }
            break;
        case 0x9000: ss << "SNE V" << std::hex << (int)x << ", V" << (int)y; break;
        case 0xA000: ss << "LD I, " << std::hex << nnn; break;
        case 0xB000: ss << "JP V0, " << std::hex << nnn; break;
        case 0xC000: ss << "RND V" << std::hex << (int)x << ", " << (int)nn; break;
        case 0xD000: ss << "DRW V" << std::hex << (int)x << ", V" << (int)y << ", " << (int)n; break;
        case 0xE000:
            if (nn == 0x9E) ss << "SKP V" << std::hex << (int)x;
            else if (nn == 0xA1) ss << "SKNP V" << std::hex << (int)x;
            break;
        case 0xF000:
            switch (nn) {
                case 0x07: ss << "LD V" << std::hex << (int)x << ", DT"; break;
                case 0x0A: ss << "LD V" << std::hex << (int)x << ", K"; break;
                case 0x15: ss << "LD DT, V" << std::hex << (int)x; break;
                case 0x18: ss << "LD ST, V" << std::hex << (int)x; break;
                case 0x1E: ss << "ADD I, V" << std::hex << (int)x; break;
                case 0x29: ss << "LD F, V" << std::hex << (int)x; break;
                case 0x33: ss << "LD B, V" << std::hex << (int)x; break;
                case 0x55: ss << "LD [I], V" << std::hex << (int)x; break;
                case 0x65: ss << "LD V" << std::hex << (int)x << ", [I]"; break;
            }
            break;
    }
    
    return ss.str();
}

// ROM-agnostic sprite detection: find DRW instructions and trace back to LD I instructions
void Decompiler::detectSpriteData() {
    // Track addresses loaded into I before DRW instructions
    std::set<uint16_t> potential_sprite_addrs;
    std::map<uint16_t, uint8_t> sprite_heights;  // addr -> height from DRW
    
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (!is_code[i]) continue;
        
        uint16_t opcode = (rom[i] << 8) | rom[i + 1];
        
        // Found a DRW instruction - trace back to find LD I
        if ((opcode & 0xF000) == 0xD000) {
            uint8_t height = opcode & 0xF;
            if (height == 0) height = 16;  // SCHIP 16x16 sprite
            
            // Look backwards for LD I, nnn (Annn) - skip built-in font addresses (0-0x50)
            for (int j = static_cast<int>(i) - 2; j >= 0 && j > static_cast<int>(i) - 20; j -= 2) {
                if (!is_code[j]) continue;
                
                uint16_t prev_op = (rom[j] << 8) | rom[j + 1];
                if ((prev_op & 0xF000) == 0xA000) {
                    uint16_t addr = prev_op & 0x0FFF;
                    // Filter out built-in font area and low addresses
                    if (addr >= 0x200 && addr < 0x200 + rom.size()) {
                        potential_sprite_addrs.insert(addr);
                        // Use max height if multiple DRWs reference same sprite
                        if (sprite_heights.find(addr) == sprite_heights.end() || 
                            sprite_heights[addr] < height) {
                            sprite_heights[addr] = height;
                        }
                    }
                    break;
                }
                // Stop if we hit control flow
                if ((prev_op & 0xF000) == 0x1000 || // JP
                    (prev_op & 0xF000) == 0x2000 || // CALL
                    prev_op == 0x00EE) {            // RET
                    break;
                }
            }
        }
    }
    
    // Validate potential sprites: check if they're in non-code regions
    int sprite_num = 0;
    for (uint16_t addr : potential_sprite_addrs) {
        size_t offset = addr - 0x200;
        uint8_t height = sprite_heights[addr];
        
        // Check if this region is not code (or at least starts with non-code)
        bool is_data_region = offset < is_code.size() && !is_code[offset];
        
        // Also accept if it's past all detected code (common for sprite data at end)
        bool is_past_code = true;
        for (size_t k = offset; k < offset + height && k < is_code.size(); k++) {
            if (is_code[k]) {
                is_past_code = false;
                break;
            }
        }
        
        if (is_data_region || is_past_code) {
            SpriteData sprite;
            sprite.address = addr;
            sprite.height = height;
            std::ostringstream ss;
            ss << "sprite_" << std::hex << std::uppercase << addr;
            sprite.name = ss.str();
            
            sprites.push_back(sprite);
            sprite_names[addr] = sprite.name;
            
            // Mark these bytes as non-code for better output
            for (size_t k = offset; k < offset + height && k < is_code.size(); k++) {
                is_code[k] = false;
            }
        }
    }
}

// Detect array access pattern: LD I, addr; ADD I, Vx; LD Vy, [I] or LD [I], Vy
bool Decompiler::matchArrayAccess(uint16_t addr, PatternMatch& match) {
    size_t offset = addr - 0x200;
    if (offset + 5 >= rom.size()) return false;
    
    uint16_t op1 = (rom[offset] << 8) | rom[offset + 1];
    uint16_t op2 = (rom[offset + 2] << 8) | rom[offset + 3];
    uint16_t op3 = (rom[offset + 4] << 8) | rom[offset + 5];
    
    // LD I, addr (Annn)
    if ((op1 & 0xF000) != 0xA000) return false;
    uint16_t base_addr = op1 & 0x0FFF;
    
    // ADD I, Vx (Fx1E)
    if ((op2 & 0xF0FF) != 0xF01E) return false;
    uint8_t index_reg = (op2 >> 8) & 0xF;
    
    // LD V0, [I] (F065) or LD [I], V0 (F055)
    bool is_read = (op3 & 0xF0FF) == 0xF065;
    bool is_write = (op3 & 0xF0FF) == 0xF055;
    if (!is_read && !is_write) return false;
    
    uint8_t value_reg = (op3 >> 8) & 0xF;
    
    match.type = is_read ? PatternMatch::Type::ARRAY_READ : PatternMatch::Type::ARRAY_WRITE;
    match.start_addr = addr;
    match.end_addr = addr + 6;
    match.params["base_addr"] = base_addr;
    match.params["index_reg"] = index_reg;
    match.params["value_reg"] = value_reg;
    
    return true;
}

// Detect for-loop pattern at loop header
// Pattern: loop body ends with ADD Vx, 1; SNE Vx, N; JP header
bool Decompiler::matchForLoop(uint16_t addr, PatternMatch& match) {
    // This is called for potential loop headers
    // Look at the back edge to see if it matches a for-loop pattern
    
    for (const auto& loop : loops) {
        if (loop.header_addr != addr) continue;
        
        // Check the back edge location
        size_t back_offset = loop.back_edge_addr - 0x200;
        if (back_offset < 4 || back_offset + 1 >= rom.size()) continue;
        
        // Look for: ADD Vx, 1; SNE Vx, N; JP header
        // or: ADD Vx, 1; SE Vx, N; (next is not JP)
        
        // Check instruction before the JP (should be SNE Vx, N)
        uint16_t sne_op = (rom[back_offset - 2] << 8) | rom[back_offset - 1];
        if ((sne_op & 0xF000) != 0x4000 && (sne_op & 0xF000) != 0x3000) continue;
        
        uint8_t counter_reg = (sne_op >> 8) & 0xF;
        uint8_t limit = sne_op & 0xFF;
        
        // Check instruction before SNE (should be ADD Vx, 1)
        if (back_offset < 6) continue;
        uint16_t add_op = (rom[back_offset - 4] << 8) | rom[back_offset - 3];
        if ((add_op & 0xF0FF) != 0x7001) continue;  // ADD Vx, 1
        if (((add_op >> 8) & 0xF) != counter_reg) continue;
        
        // Looks like a for-loop!
        match.type = PatternMatch::Type::FOR_LOOP;
        match.start_addr = addr;
        match.end_addr = loop.back_edge_addr + 2;
        match.params["counter_reg"] = counter_reg;
        match.params["limit"] = limit;
        match.params["back_edge"] = loop.back_edge_addr;
        
        return true;
    }
    
    return false;
}

} // namespace chip8
