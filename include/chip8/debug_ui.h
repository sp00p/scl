/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * ImGui-based debug interface with memory editor, registers, disassembly,
 * breakpoints, watch expressions, source mapping, and decompiler view.
 */

#pragma once

#include <SDL2/SDL.h>
#include <chip8/compiler/source_map.h>
#include <chip8/decompiler.h>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class Chip8;

class DebugUI {
public:
    DebugUI();
    ~DebugUI();

    bool init();
    void shutdown();
    bool processEvent(SDL_Event& event);
    void update(Chip8& chip8);

    bool isPaused() const { return paused; }
    bool shouldStep();
    bool isBreakpoint(uint16_t addr, Chip8* chip8 = nullptr) const;
    void pause() { paused = true; }
    bool isOpen() const { return window != nullptr; }
    Uint32 getWindowID() const;
    bool loadSourceMap(const std::string& path);

    void setRomLoadCallback(std::function<void(const std::string&)> callback) {
        rom_load_callback = callback;
    }

private:
    void renderMemoryEditor(Chip8& chip8);
    void renderRegisters(Chip8& chip8);
    void renderStack(Chip8& chip8);
    void renderDisassembly(Chip8& chip8);
    void renderControls(Chip8& chip8);
    void renderDisplay(Chip8& chip8);
    void renderSourcePanel(Chip8& chip8);
    void renderWatchPanel(Chip8& chip8);
    void renderRomBrowser();
    void renderDecompiler(Chip8& chip8);

    std::string disassemble(uint16_t opcode) const;
    std::vector<std::string> listRomFiles(const std::string& directory);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    bool initialized = false;
    bool paused = true;
    bool step_requested = false;

    struct BreakpointInfo {
        std::string condition;
    };
    std::map<uint16_t, BreakpointInfo> breakpoints;
    char breakpoint_input[8] = "";
    char condition_input[32] = "";

    bool evaluateCondition(const std::string& cond, Chip8& chip8);

    chip8::compiler::SourceMap source_map;
    bool has_source_map = false;

    int selected_source_line = 0;
    uint16_t selected_asm_address = 0;
    bool highlight_from_source = false;

    struct WatchEntry {
        std::string expression;
        std::string last_value;
    };
    std::vector<WatchEntry> watch_list;
    char watch_input[64] = "";

    std::string evaluateWatch(const std::string& expr, Chip8& chip8);

    std::string current_browse_dir;
    std::vector<std::string> directory_entries;
    bool show_rom_browser = false;
    std::function<void(const std::string&)> rom_load_callback;

    chip8::Decompiler decompiler;
    std::string decompiled_code;
    bool decompiler_needs_refresh = true;
};
