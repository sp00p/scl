/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Source map for debugging. Maps bytecode addresses to source locations.
 */

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

struct SourceMapping {
    uint16_t address;
    int line;
    int column;
    std::string filename;
};

class SourceMap {
public:
    SourceMap() = default;

    void addMapping(uint16_t address, int line, int column = 1, const std::string& filename = "");
    bool getSourceLocation(uint16_t address, int& line, int& column, std::string& filename) const;
    bool getAddress(int line, uint16_t& address) const;
    std::vector<int> getBreakpointableLines() const;
    std::vector<uint16_t> getAddressesForLine(int line) const;
    int getLineForAddress(uint16_t address) const;
    bool save(const std::string& filepath) const;
    bool load(const std::string& filepath);
    void clear();
    void setSource(const std::string& source);
    const std::string& getSource() const { return source_code; }
    std::string getSourceLine(int line) const;
    int getLineCount() const { return static_cast<int>(source_lines.size()); }

private:
    std::vector<SourceMapping> mappings;
    std::map<uint16_t, size_t> address_to_mapping;
    std::map<int, uint16_t> line_to_address;
    std::string source_code;
    std::vector<std::string> source_lines;
};

}
}
