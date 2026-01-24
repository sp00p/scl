/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Preprocessor for #include and #define directives.
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

class Preprocessor {
public:
    Preprocessor();

    std::string process(const std::string& source, const std::string& filename = "");
    std::string processFile(const std::string& filepath);
    void addIncludePath(const std::string& path);
    void define(const std::string& name, const std::string& value = "1");
    const std::vector<std::string>& getIncludedFiles() const { return included_files; }
    const std::vector<std::string>& getErrors() const { return errors; }
    bool hasErrors() const { return !errors.empty(); }

private:
    std::string processLine(const std::string& line, const std::string& current_dir, int line_num);
    std::string resolveInclude(const std::string& include_path, const std::string& current_dir);
    std::string expandMacros(const std::string& line);
    std::string readFile(const std::string& filepath);
    std::string getDirectory(const std::string& filepath);

    std::vector<std::string> include_paths;
    std::vector<std::string> included_files;
    std::set<std::string> include_guard;
    std::map<std::string, std::string> defines;
    std::vector<std::string> errors;
};

}
}
