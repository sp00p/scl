#include <chip8/compiler/preprocessor.h>
#include <fstream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

namespace chip8 {
namespace compiler {

Preprocessor::Preprocessor() = default;

std::string Preprocessor::getDirectory(const std::string& filepath) {
    size_t pos = filepath.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return filepath.substr(0, pos);
}

std::string Preprocessor::readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string Preprocessor::resolveInclude(const std::string& include_path, const std::string& current_dir) {
    // Try relative to current file first
    std::string full_path = current_dir + PATH_SEP + include_path;
    std::ifstream test(full_path);
    if (test.is_open()) {
        test.close();
        return full_path;
    }
    
    // Try include paths
    for (const auto& dir : include_paths) {
        full_path = dir + PATH_SEP + include_path;
        test.open(full_path);
        if (test.is_open()) {
            test.close();
            return full_path;
        }
    }
    
    // Try as absolute/relative to cwd
    test.open(include_path);
    if (test.is_open()) {
        test.close();
        return include_path;
    }
    
    return "";
}

std::string Preprocessor::expandMacros(const std::string& line) {
    std::string result = line;
    
    // Expand each defined macro (longest match first to handle overlapping names)
    // Sort defines by length (longest first) to avoid partial replacements
    std::vector<std::pair<std::string, std::string>> sorted_defines(defines.begin(), defines.end());
    std::sort(sorted_defines.begin(), sorted_defines.end(),
        [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
    
    for (const auto& [name, value] : sorted_defines) {
        size_t pos = 0;
        while ((pos = result.find(name, pos)) != std::string::npos) {
            // Check that it's a whole word (not part of another identifier)
            bool is_word_start = (pos == 0 || !std::isalnum(result[pos - 1]) && result[pos - 1] != '_');
            bool is_word_end = (pos + name.length() >= result.length() || 
                               (!std::isalnum(result[pos + name.length()]) && result[pos + name.length()] != '_'));
            
            if (is_word_start && is_word_end) {
                result.replace(pos, name.length(), value);
                pos += value.length();
            } else {
                pos += name.length();
            }
        }
    }
    
    return result;
}

std::string Preprocessor::processLine(const std::string& line, const std::string& current_dir, int line_num) {
    // Trim whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return line;
    
    // Check for preprocessor directives
    if (line[start] == '#') {
        size_t directive_start = start + 1;
        while (directive_start < line.size() && (line[directive_start] == ' ' || line[directive_start] == '\t')) {
            directive_start++;
        }
        
        // #define NAME value
        if (line.compare(directive_start, 6, "define") == 0) {
            size_t name_start = directive_start + 6;
            while (name_start < line.size() && (line[name_start] == ' ' || line[name_start] == '\t')) {
                name_start++;
            }
            
            // Find end of name
            size_t name_end = name_start;
            while (name_end < line.size() && (std::isalnum(line[name_end]) || line[name_end] == '_')) {
                name_end++;
            }
            
            if (name_end > name_start) {
                std::string name = line.substr(name_start, name_end - name_start);
                
                // Find value (rest of line after whitespace)
                size_t value_start = name_end;
                while (value_start < line.size() && (line[value_start] == ' ' || line[value_start] == '\t')) {
                    value_start++;
                }
                
                std::string value = (value_start < line.size()) ? line.substr(value_start) : "1";
                // Trim trailing whitespace/comments from value
                size_t comment_pos = value.find("//");
                if (comment_pos != std::string::npos) {
                    value = value.substr(0, comment_pos);
                }
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
                    value.pop_back();
                }
                if (value.empty()) value = "1";
                
                defines[name] = value;
                return "// #define " + name + " " + value + "\n";
            } else {
                errors.push_back("Line " + std::to_string(line_num) + ": Invalid #define syntax");
                return "// ERROR: Invalid #define syntax\n";
            }
        }
        
        // #include
        if (line.compare(directive_start, 7, "include") == 0) {
            // Parse include path
            size_t quote_start = line.find('"', directive_start + 7);
            size_t quote_end = line.find('"', quote_start + 1);
            
            if (quote_start == std::string::npos || quote_end == std::string::npos) {
                // Try angle brackets
                quote_start = line.find('<', directive_start + 7);
                quote_end = line.find('>', quote_start + 1);
            }
            
            if (quote_start != std::string::npos && quote_end != std::string::npos) {
                std::string include_path = line.substr(quote_start + 1, quote_end - quote_start - 1);
                std::string resolved = resolveInclude(include_path, current_dir);
                
                if (resolved.empty()) {
                    errors.push_back("Line " + std::to_string(line_num) + ": Cannot find include file: " + include_path);
                    return "// ERROR: Cannot find " + include_path + "\n";
                }
                
                // Check for circular include
                if (include_guard.count(resolved)) {
                    return "// Already included: " + include_path + "\n";
                }
                
                include_guard.insert(resolved);
                included_files.push_back(resolved);
                
                // Recursively process included file
                std::string content = readFile(resolved);
                if (content.empty()) {
                    errors.push_back("Line " + std::to_string(line_num) + ": Cannot read include file: " + resolved);
                    return "// ERROR: Cannot read " + include_path + "\n";
                }
                
                std::string include_dir = getDirectory(resolved);
                return "// BEGIN " + include_path + "\n" + 
                       process(content, resolved) + 
                       "\n// END " + include_path + "\n";
            } else {
                errors.push_back("Line " + std::to_string(line_num) + ": Invalid #include syntax");
                return "// ERROR: Invalid #include syntax\n";
            }
        }
    }
    
    // Expand macros in non-directive lines
    return expandMacros(line);
}

std::string Preprocessor::process(const std::string& source, const std::string& filename) {
    std::string current_dir = filename.empty() ? "." : getDirectory(filename);
    
    std::istringstream stream(source);
    std::ostringstream result;
    std::string line;
    int line_num = 0;
    
    while (std::getline(stream, line)) {
        line_num++;
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        result << processLine(line, current_dir, line_num) << "\n";
    }
    
    return result.str();
}

std::string Preprocessor::processFile(const std::string& filepath) {
    std::string content = readFile(filepath);
    if (content.empty()) {
        errors.push_back("Cannot open file: " + filepath);
        return "";
    }
    
    include_guard.insert(filepath);
    included_files.push_back(filepath);
    
    return process(content, filepath);
}

void Preprocessor::addIncludePath(const std::string& path) {
    include_paths.push_back(path);
}

void Preprocessor::define(const std::string& name, const std::string& value) {
    defines[name] = value;
}

} // namespace compiler
} // namespace chip8
