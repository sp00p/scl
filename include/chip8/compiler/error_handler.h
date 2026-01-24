/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Compilation error handling with source context and formatting.
 */

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

struct CompilationError {
    std::string message;
    int line;
    int column;
    std::string filename;
    std::string source_line;

    CompilationError(const std::string& msg, int l, int c, const std::string& file = "",
                     const std::string& src_line = "")
        : message(msg), line(l), column(c), filename(file), source_line(src_line) {}

    std::string format() const;
};

class CompilationException : public std::runtime_error {
public:
    CompilationException(const CompilationError& error);
    const CompilationError& getError() const { return error; }

private:
    CompilationError error;
};

class ErrorHandler {
public:
    ErrorHandler(bool throwOnError = true);

    void setSource(const std::string& source);
    void error(const std::string& message, int line, int column, const std::string& filename = "");
    void warning(const std::string& message, int line, int column, const std::string& filename = "");
    bool hasErrors() const { return !errors.empty(); }
    bool hasWarnings() const { return !warnings.empty(); }
    const std::vector<CompilationError>& getErrors() const { return errors; }
    const std::vector<CompilationError>& getWarnings() const { return warnings; }
    void reset();

private:
    std::vector<CompilationError> errors;
    std::vector<CompilationError> warnings;
    std::vector<std::string> source_lines;
    bool throwOnError;

    std::string getSourceLine(int line) const;
};

}
}
