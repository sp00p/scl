/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Symbol table for tracking variables and functions across scopes.
 */

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace chip8 {
namespace compiler {

enum class SymbolType {
    VARIABLE,
    FUNCTION
};

struct Symbol {
    std::string name;
    SymbolType type;
    uint8_t register_id;
    uint16_t address;

    Symbol(const std::string& n, SymbolType t)
        : name(n), type(t), register_id(0), address(0) {}
};

class Scope {
public:
    Scope(Scope* parent = nullptr) : parent(parent) {}

    void define(const Symbol& symbol);
    bool isDefined(const std::string& name) const;
    Symbol* resolve(const std::string& name);
    Scope* getParent() const { return parent; }

private:
    std::map<std::string, Symbol> symbols;
    Scope* parent;
};

class SymbolTable {
public:
    SymbolTable();

    void enterScope();
    void exitScope();
    void define(const Symbol& symbol);
    Symbol* resolve(const std::string& name);
    bool isDefined(const std::string& name) const;

private:
    std::vector<std::unique_ptr<Scope>> scopes;
    Scope* current;
};

}
}
