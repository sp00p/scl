/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Main compiler driver. Orchestrates lexing, parsing, and code generation
 * to produce CHIP-8 bytecode from SCL source.
 */

#pragma once

#include "codegen.h"
#include "error_handler.h"
#include "lexer.h"
#include "parser.h"
#include <memory>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

class Compiler {
public:
    Compiler();
    void compile(const std::string& source_file, const std::string& output_file);
    void setDebugMode(bool enable) { debug_mode = enable; }
    bool isDebugMode() const { return debug_mode; }

private:
    Lexer lexer;
    std::unique_ptr<Parser> parser;
    std::unique_ptr<CodeGenerator> codeGen;
    ErrorHandler errorHandler;
    bool debug_mode;
};

}
}
