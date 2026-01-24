/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * CHIP-8 and Super CHIP-8 disassembler. Converts binary ROMs to
 * human-readable assembly with code/data section analysis.
 */

#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace chip8 {

struct DisassembledInstruction {
    uint16_t address;
    uint16_t opcode;
    std::string mnemonic;
    std::string comment;
    bool is_data = false;
};

class Disassembler {
public:
    Disassembler();

    std::vector<DisassembledInstruction> disassembleFile(const std::string& filepath);
    std::vector<DisassembledInstruction> disassemble(const std::vector<uint8_t>& rom);
    static std::string disassembleOpcode(uint16_t opcode, bool schip = true);
    void output(std::ostream& out, const std::vector<DisassembledInstruction>& instructions,
                bool show_bytes = true, bool show_address = true);
    void setSchipMode(bool enabled) { schip_mode = enabled; }

private:
    bool schip_mode = true;
    void analyzeCodeSections(const std::vector<uint8_t>& rom, std::vector<bool>& is_code);
};

int runDisassembler(const std::string& input_file, const std::string& output_file);

}
