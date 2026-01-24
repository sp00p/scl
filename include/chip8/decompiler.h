/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * ROM decompiler with control flow analysis. Detects functions, loops,
 * branches, and common patterns to generate readable pseudo-code.
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace chip8 {

struct CFGInstruction {
    uint16_t address;
    uint16_t opcode;
    std::string mnemonic;

    uint8_t x() const { return (opcode >> 8) & 0xF; }
    uint8_t y() const { return (opcode >> 4) & 0xF; }
    uint8_t n() const { return opcode & 0xF; }
    uint8_t nn() const { return opcode & 0xFF; }
    uint16_t nnn() const { return opcode & 0xFFF; }

    bool isJump() const { return (opcode & 0xF000) == 0x1000; }
    bool isCall() const { return (opcode & 0xF000) == 0x2000; }
    bool isReturn() const { return opcode == 0x00EE; }
    bool isSkip() const;
    bool isConditional() const { return isSkip(); }
};

struct BasicBlock {
    uint16_t start_addr;
    uint16_t end_addr;
    std::vector<CFGInstruction> instructions;
    std::vector<uint16_t> successors;
    std::vector<uint16_t> predecessors;
    bool is_loop_header = false;
    bool is_function_entry = false;
    bool is_data = false;
};

struct Function {
    std::string name;
    uint16_t entry_addr;
    std::vector<uint16_t> block_addresses;
    std::set<uint8_t> used_registers;
    bool returns_value = false;
};

struct LoopInfo {
    uint16_t header_addr;
    uint16_t back_edge_addr;
    std::set<uint16_t> body_blocks;
    enum class LoopType { WHILE, DO_WHILE, INFINITE } type = LoopType::WHILE;
};

struct FunctionReachability {
    std::set<uint16_t> reachable_addrs;
    uint16_t min_addr = 0xFFFF;
    uint16_t max_addr = 0;
};

struct BranchInfo {
    uint16_t condition_addr;
    uint16_t then_addr;
    uint16_t else_addr;
    uint16_t merge_addr;
};

struct PatternMatch {
    enum class Type {
        WAIT_DELAY,
        KEY_CHECK,
        WAIT_KEY,
        DRAW_SPRITE,
        DRAW_DIGIT,
        CLEAR_SCREEN,
        BEEP,
        RANDOM,
        BCD_CONVERT,
        MEMORY_LOAD,
        MEMORY_STORE,
        EQUALITY_CHECK,
        LESS_THAN,
        KEY_PRESSED_IF,
        BOOL_COMPARE_IF,
        ARRAY_READ,
        ARRAY_WRITE,
        FOR_LOOP,
    };

    Type type;
    uint16_t start_addr;
    uint16_t end_addr;
    std::map<std::string, int> params;
};

struct SpriteData {
    uint16_t address;
    uint8_t height;
    std::string name;
};

class Decompiler {
public:
    Decompiler();

    void analyze(const std::vector<uint8_t>& rom);
    std::string decompile();
    std::string decompileFunction(uint16_t entry_addr);
    std::string decompileRange(uint16_t start_addr, uint16_t end_addr);

    const std::map<uint16_t, BasicBlock>& getBlocks() const { return blocks; }
    const std::vector<Function>& getFunctions() const { return functions; }
    const std::vector<LoopInfo>& getLoops() const { return loops; }

    bool show_addresses = false;
    bool use_variable_names = true;

private:
    std::vector<uint8_t> rom;
    std::map<uint16_t, BasicBlock> blocks;
    std::vector<Function> functions;
    std::vector<LoopInfo> loops;
    std::vector<BranchInfo> branches;
    std::vector<PatternMatch> patterns;
    std::vector<bool> is_code;
    std::map<uint8_t, std::string> register_names;
    std::map<uint16_t, FunctionReachability> function_reach;
    std::set<uint16_t> emitted_addresses;
    std::set<uint16_t> processing_loop_headers;

    void buildCFG();
    void identifyBasicBlocks();
    void connectBlocks();
    void detectFunctions();
    void computeFunctionReachability();
    void detectLoops();
    void detectBranches();
    void detectIfElse();
    void matchPatterns();
    bool matchWaitDelay(uint16_t addr, PatternMatch& match);
    bool matchKeyCheck(uint16_t addr, PatternMatch& match);
    bool matchKeyExpression(uint16_t addr, PatternMatch& match);
    bool matchEqualityCheck(uint16_t addr, PatternMatch& match);
    bool matchLessThan(uint16_t addr, PatternMatch& match);
    bool matchKeyPressedIf(uint16_t addr, PatternMatch& match);
    bool matchBoolCompareIf(uint16_t addr, PatternMatch& match);
    bool matchArrayAccess(uint16_t addr, PatternMatch& match);
    bool matchForLoop(uint16_t addr, PatternMatch& match);
    void detectSpriteData();
    std::vector<SpriteData> sprites;
    std::map<uint16_t, std::string> sprite_names;
    void inferVariableNames();
    std::string generateStructuredCode(uint16_t start_addr, uint16_t end_addr, int indent);
    std::string generateBlock(uint16_t addr, int indent, uint16_t stop_addr = 0);
    std::string generateStatement(const CFGInstruction& instr, int indent);
    std::string generateExpression(const CFGInstruction& instr);
    std::string indent_str(int level) const;
    std::string regName(uint8_t reg) const;
    bool isLoopHeader(uint16_t addr) const;
    bool isIfPattern(uint16_t addr, uint16_t& then_addr, uint16_t& else_addr, uint16_t& end_addr) const;
    uint16_t getLoopEnd(uint16_t header_addr) const;
    CFGInstruction getInstruction(uint16_t addr) const;
    const PatternMatch* getPatternAt(uint16_t addr) const;
    static std::string disassembleOpcode(uint16_t opcode);
};

}
