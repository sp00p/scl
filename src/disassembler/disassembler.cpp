#include <chip8/disassembler.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <set>

namespace chip8 {

Disassembler::Disassembler() = default;

std::string Disassembler::disassembleOpcode(uint16_t opcode, bool schip) {
    std::ostringstream ss;
    
    uint8_t x = (opcode & 0x0F00) >> 8;
    uint8_t y = (opcode & 0x00F0) >> 4;
    uint8_t n = opcode & 0x000F;
    uint8_t nn = opcode & 0x00FF;
    uint16_t nnn = opcode & 0x0FFF;
    
    switch (opcode & 0xF000) {
        case 0x0000:
            switch (opcode) {
                case 0x00E0: return "CLS";
                case 0x00EE: return "RET";
                case 0x00FB: return schip ? "SCR" : "???";
                case 0x00FC: return schip ? "SCL" : "???";
                case 0x00FD: return schip ? "EXIT" : "???";
                case 0x00FE: return schip ? "LOW" : "???";
                case 0x00FF: return schip ? "HIGH" : "???";
                default:
                    if ((opcode & 0xFFF0) == 0x00C0 && schip) {
                        ss << "SCD " << (int)n;
                        return ss.str();
                    }
                    ss << "SYS 0x" << std::hex << std::uppercase << nnn;
                    return ss.str();
            }
            
        case 0x1000:
            ss << "JP 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(3) << nnn;
            return ss.str();
            
        case 0x2000:
            ss << "CALL 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(3) << nnn;
            return ss.str();
            
        case 0x3000:
            ss << "SE V" << std::hex << std::uppercase << (int)x << ", 0x" << std::setfill('0') << std::setw(2) << (int)nn;
            return ss.str();
            
        case 0x4000:
            ss << "SNE V" << std::hex << std::uppercase << (int)x << ", 0x" << std::setfill('0') << std::setw(2) << (int)nn;
            return ss.str();
            
        case 0x5000:
            if (n == 0) {
                ss << "SE V" << std::hex << std::uppercase << (int)x << ", V" << (int)y;
                return ss.str();
            }
            break;
            
        case 0x6000:
            ss << "LD V" << std::hex << std::uppercase << (int)x << ", 0x" << std::setfill('0') << std::setw(2) << (int)nn;
            return ss.str();
            
        case 0x7000:
            ss << "ADD V" << std::hex << std::uppercase << (int)x << ", 0x" << std::setfill('0') << std::setw(2) << (int)nn;
            return ss.str();
            
        case 0x8000:
            switch (n) {
                case 0x0: ss << "LD V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x1: ss << "OR V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x2: ss << "AND V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x3: ss << "XOR V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x4: ss << "ADD V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x5: ss << "SUB V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x6: ss << "SHR V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0x7: ss << "SUBN V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
                case 0xE: ss << "SHL V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; return ss.str();
            }
            break;
            
        case 0x9000:
            if (n == 0) {
                ss << "SNE V" << std::hex << std::uppercase << (int)x << ", V" << (int)y;
                return ss.str();
            }
            break;
            
        case 0xA000:
            ss << "LD I, 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(3) << nnn;
            return ss.str();
            
        case 0xB000:
            ss << "JP V0, 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(3) << nnn;
            return ss.str();
            
        case 0xC000:
            ss << "RND V" << std::hex << std::uppercase << (int)x << ", 0x" << std::setfill('0') << std::setw(2) << (int)nn;
            return ss.str();
            
        case 0xD000:
            if (n == 0 && schip) {
                ss << "DRW V" << std::hex << std::uppercase << (int)x << ", V" << (int)y << ", 0";
            } else {
                ss << "DRW V" << std::hex << std::uppercase << (int)x << ", V" << (int)y << ", " << std::dec << (int)n;
            }
            return ss.str();
            
        case 0xE000:
            switch (nn) {
                case 0x9E: ss << "SKP V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0xA1: ss << "SKNP V" << std::hex << std::uppercase << (int)x; return ss.str();
            }
            break;
            
        case 0xF000:
            switch (nn) {
                case 0x07: ss << "LD V" << std::hex << std::uppercase << (int)x << ", DT"; return ss.str();
                case 0x0A: ss << "LD V" << std::hex << std::uppercase << (int)x << ", K"; return ss.str();
                case 0x15: ss << "LD DT, V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0x18: ss << "LD ST, V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0x1E: ss << "ADD I, V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0x29: ss << "LD F, V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0x30: 
                    if (schip) {
                        ss << "LD HF, V" << std::hex << std::uppercase << (int)x;
                        return ss.str();
                    }
                    break;
                case 0x33: ss << "LD B, V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0x55: ss << "LD [I], V" << std::hex << std::uppercase << (int)x; return ss.str();
                case 0x65: ss << "LD V" << std::hex << std::uppercase << (int)x << ", [I]"; return ss.str();
                case 0x75:
                    if (schip) {
                        ss << "LD R, V" << std::hex << std::uppercase << (int)x;
                        return ss.str();
                    }
                    break;
                case 0x85:
                    if (schip) {
                        ss << "LD V" << std::hex << std::uppercase << (int)x << ", R";
                        return ss.str();
                    }
                    break;
            }
            break;
    }
    
    ss << "DW 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << opcode;
    return ss.str();
}

std::vector<DisassembledInstruction> Disassembler::disassembleFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> rom(size);
    if (!file.read(reinterpret_cast<char*>(rom.data()), size)) {
        throw std::runtime_error("Failed to read file: " + filepath);
    }
    
    return disassemble(rom);
}

void Disassembler::analyzeCodeSections(const std::vector<uint8_t>& rom,
                                        std::vector<bool>& is_code) {
    is_code.resize(rom.size(), false);
    if (rom.size() < 2) return;
    
    std::set<uint16_t> visited;
    std::vector<uint16_t> work_list;
    work_list.push_back(0);
    
    while (!work_list.empty()) {
        uint16_t offset = work_list.back();
        work_list.pop_back();
        
        if (offset >= rom.size() - 1 || visited.count(offset)) continue;
        visited.insert(offset);
        
        is_code[offset] = true;
        is_code[offset + 1] = true;
        
        uint16_t opcode = (rom[offset] << 8) | rom[offset + 1];
        uint16_t nnn = opcode & 0x0FFF;
        
        switch (opcode & 0xF000) {
            case 0x1000:
                if (nnn >= 0x200 && nnn - 0x200 < rom.size()) {
                    work_list.push_back(nnn - 0x200);
                }
                break;
                
            case 0x2000:
                if (nnn >= 0x200 && nnn - 0x200 < rom.size()) {
                    work_list.push_back(nnn - 0x200);
                }
                work_list.push_back(offset + 2);
                break;
                
            case 0x0000:
                if (opcode == 0x00EE) {
                } else if (opcode == 0x00FD) {
                } else {
                    work_list.push_back(offset + 2);
                }
                break;
                
            case 0x3000:
            case 0x4000:
            case 0x5000:
            case 0x9000:
            case 0xE000:
                work_list.push_back(offset + 2);
                work_list.push_back(offset + 4);
                break;
                
            default:
                work_list.push_back(offset + 2);
                break;
        }
    }
}

std::vector<DisassembledInstruction> Disassembler::disassemble(const std::vector<uint8_t>& rom) {
    std::vector<DisassembledInstruction> result;
    
    std::vector<bool> is_code;
    analyzeCodeSections(rom, is_code);
    
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        DisassembledInstruction instr;
        instr.address = 0x200 + static_cast<uint16_t>(i);
        instr.opcode = (rom[i] << 8) | rom[i + 1];
        instr.is_data = !is_code[i];
        
        if (instr.is_data) {
            std::ostringstream ss;
            ss << "DW 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << instr.opcode;
            instr.mnemonic = ss.str();
            instr.comment = "; data";
        } else {
            instr.mnemonic = disassembleOpcode(instr.opcode, schip_mode);
            
            uint16_t nnn = instr.opcode & 0x0FFF;
            switch (instr.opcode & 0xF000) {
                case 0x2000:
                    instr.comment = "; call subroutine";
                    break;
                case 0x1000:
                    if (nnn == instr.address) {
                        instr.comment = "; infinite loop (halt)";
                    }
                    break;
            }
        }
        
        result.push_back(instr);
    }
    
    if (rom.size() % 2 == 1) {
        DisassembledInstruction instr;
        instr.address = 0x200 + static_cast<uint16_t>(rom.size() - 1);
        instr.opcode = rom.back();
        instr.is_data = true;
        std::ostringstream ss;
        ss << "DB 0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)rom.back();
        instr.mnemonic = ss.str();
        result.push_back(instr);
    }
    
    return result;
}

void Disassembler::output(std::ostream& out, const std::vector<DisassembledInstruction>& instructions,
                          bool show_bytes, bool show_address) {
    out << "; CHIP-8 Disassembly\n";
    out << "; Generated by SCL 2.0\n\n";
    
    for (const auto& instr : instructions) {
        if (show_address) {
            out << std::hex << std::uppercase << std::setfill('0') << std::setw(3) << instr.address << ":  ";
        }
        
        if (show_bytes) {
            out << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << instr.opcode << "  ";
        }
        
        out << std::left << std::setfill(' ') << std::setw(20) << instr.mnemonic;
        
        if (!instr.comment.empty()) {
            out << "  " << instr.comment;
        }
        
        out << "\n";
    }
}

int runDisassembler(const std::string& input_file, const std::string& output_file) {
    try {
        Disassembler disasm;
        auto instructions = disasm.disassembleFile(input_file);
        
        if (output_file.empty() || output_file == "-") {
            disasm.output(std::cout, instructions);
        } else {
            std::ofstream out(output_file);
            if (!out.is_open()) {
                std::cerr << "Error: Cannot open output file: " << output_file << std::endl;
                return 1;
            }
            disasm.output(out, instructions);
            std::cout << "Disassembled " << instructions.size() << " instructions to " << output_file << std::endl;
        }
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Disassembly error: " << e.what() << std::endl;
        return 1;
    }
}

}
