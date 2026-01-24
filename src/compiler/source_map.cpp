#include <chip8/compiler/source_map.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace chip8 {
namespace compiler {

void SourceMap::addMapping(uint16_t address, int line, int column, const std::string& filename) {
    SourceMapping mapping;
    mapping.address = address;
    mapping.line = line;
    mapping.column = column;
    mapping.filename = filename;
    
    size_t idx = mappings.size();
    mappings.push_back(mapping);
    address_to_mapping[address] = idx;
    
    // Track first address for each line
    if (line_to_address.find(line) == line_to_address.end()) {
        line_to_address[line] = address;
    }
}

bool SourceMap::getSourceLocation(uint16_t address, int& line, int& column, std::string& filename) const {
    auto it = address_to_mapping.find(address);
    if (it != address_to_mapping.end()) {
        const auto& mapping = mappings[it->second];
        line = mapping.line;
        column = mapping.column;
        filename = mapping.filename;
        return true;
    }
    
    // Find closest mapping before this address
    uint16_t closest = 0;
    bool found = false;
    for (const auto& m : mappings) {
        if (m.address <= address && m.address > closest) {
            closest = m.address;
            found = true;
        }
    }
    
    if (found) {
        auto idx = address_to_mapping.at(closest);
        const auto& mapping = mappings[idx];
        line = mapping.line;
        column = mapping.column;
        filename = mapping.filename;
        return true;
    }
    
    return false;
}

bool SourceMap::getAddress(int line, uint16_t& address) const {
    auto it = line_to_address.find(line);
    if (it != line_to_address.end()) {
        address = it->second;
        return true;
    }
    return false;
}

std::vector<int> SourceMap::getBreakpointableLines() const {
    std::vector<int> lines;
    lines.reserve(line_to_address.size());
    for (const auto& pair : line_to_address) {
        lines.push_back(pair.first);
    }
    std::sort(lines.begin(), lines.end());
    return lines;
}

std::vector<uint16_t> SourceMap::getAddressesForLine(int line) const {
    std::vector<uint16_t> addresses;
    for (const auto& m : mappings) {
        if (m.line == line) {
            addresses.push_back(m.address);
        }
    }
    std::sort(addresses.begin(), addresses.end());
    return addresses;
}

int SourceMap::getLineForAddress(uint16_t address) const {
    int line = 0, col = 0;
    std::string filename;
    if (getSourceLocation(address, line, col, filename)) {
        return line;
    }
    return 0;
}

bool SourceMap::save(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"mappings\": [\n";
    
    for (size_t i = 0; i < mappings.size(); ++i) {
        const auto& m = mappings[i];
        file << "    {\"addr\": " << m.address 
             << ", \"line\": " << m.line 
             << ", \"col\": " << m.column;
        if (!m.filename.empty()) {
            file << ", \"file\": \"" << m.filename << "\"";
        }
        file << "}";
        if (i + 1 < mappings.size()) file << ",";
        file << "\n";
    }
    
    file << "  ]\n";
    file << "}\n";
    
    return true;
}

bool SourceMap::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    clear();
    
    // Simple JSON parser (handles our specific format)
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    
    size_t pos = 0;
    while ((pos = content.find("\"addr\":", pos)) != std::string::npos) {
        pos += 7;
        uint16_t addr = static_cast<uint16_t>(std::stoul(content.substr(pos)));
        
        pos = content.find("\"line\":", pos);
        if (pos == std::string::npos) break;
        pos += 7;
        int line = std::stoi(content.substr(pos));
        
        pos = content.find("\"col\":", pos);
        if (pos == std::string::npos) break;
        pos += 6;
        int col = std::stoi(content.substr(pos));
        
        std::string filename;
        size_t file_pos = content.find("\"file\":", pos);
        size_t brace_pos = content.find("}", pos);
        if (file_pos != std::string::npos && file_pos < brace_pos) {
            size_t start = content.find("\"", file_pos + 7) + 1;
            size_t end = content.find("\"", start);
            filename = content.substr(start, end - start);
        }
        
        addMapping(addr, line, col, filename);
    }
    
    return !mappings.empty();
}

void SourceMap::clear() {
    mappings.clear();
    address_to_mapping.clear();
    line_to_address.clear();
}

void SourceMap::setSource(const std::string& source) {
    source_code = source;
    source_lines.clear();
    
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        // Remove trailing \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        source_lines.push_back(line);
    }
}

std::string SourceMap::getSourceLine(int line) const {
    if (line < 1 || line > static_cast<int>(source_lines.size())) {
        return "";
    }
    return source_lines[line - 1];
}

} // namespace compiler
} // namespace chip8
