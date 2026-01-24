#include <chip8/debug_ui.h>
#include <chip8/emulator.h>
#include <chip8/runtime_config.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_memory_editor.h"

#include <cstdio>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// Static memory editor instance
static MemoryEditor mem_editor;

DebugUI::DebugUI() = default;

DebugUI::~DebugUI() {
    if (initialized) {
        shutdown();
    }
}

bool DebugUI::init() {
    // Create a separate debug window
    window = SDL_CreateWindow("CHIP-8 Debugger",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              900, 700,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        SDL_DestroyWindow(window);
        window = nullptr;
        return false;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Setup style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // Configure memory editor
    mem_editor.OptShowOptions = true;
    mem_editor.OptShowAscii = true;
    mem_editor.OptUpperCaseHex = true;

    initialized = true;
    return true;
}

void DebugUI::shutdown() {
    if (!initialized) return;

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    initialized = false;
}

Uint32 DebugUI::getWindowID() const {
    return window ? SDL_GetWindowID(window) : 0;
}

bool DebugUI::processEvent(SDL_Event& event) {
    if (!initialized || !window) return false;

    // Check if this event is for our window
    Uint32 debugWindowID = SDL_GetWindowID(window);
    bool isOurEvent = false;

    if (event.type == SDL_WINDOWEVENT && event.window.windowID == debugWindowID) {
        isOurEvent = true;
    } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ||
               event.type == SDL_TEXTINPUT || event.type == SDL_MOUSEMOTION ||
               event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
               event.type == SDL_MOUSEWHEEL) {
        // Check if mouse/keyboard event is for our window
        SDL_Window* focused = SDL_GetKeyboardFocus();
        if (focused && SDL_GetWindowID(focused) == debugWindowID) {
            isOurEvent = true;
        }
    }

    if (isOurEvent) {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }

    return isOurEvent;
}

bool DebugUI::shouldStep() {
    if (step_requested) {
        step_requested = false;
        return true;
    }
    return false;
}

bool DebugUI::isBreakpoint(uint16_t addr, Chip8* chip8) const {
    auto it = breakpoints.find(addr);
    if (it == breakpoints.end()) return false;
    
    // Unconditional breakpoint
    if (it->second.condition.empty()) return true;
    
    // Conditional breakpoint - evaluate condition
    if (chip8) {
        return const_cast<DebugUI*>(this)->evaluateCondition(it->second.condition, *chip8);
    }
    return true;
}

void DebugUI::update(Chip8& chip8) {
    if (!initialized || !window) return;

    // Start ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Create a full-window layout
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Debug", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Top bar with controls
    renderControls(chip8);

    ImGui::Separator();

    // Main content area
    float contentHeight = ImGui::GetContentRegionAvail().y;

    // Left panel: Registers + Stack
    ImGui::BeginChild("LeftPanel", ImVec2(200, contentHeight), true);
    renderRegisters(chip8);
    ImGui::Separator();
    renderStack(chip8);
    ImGui::EndChild();

    ImGui::SameLine();

    // Middle panel: Side-by-side Source + Disassembly (Compiler Explorer style)
    float middleWidth = has_source_map ? 550 : 350;
    ImGui::BeginChild("MiddlePanel", ImVec2(middleWidth, contentHeight), true);
    
    if (has_source_map) {
        // Side-by-side layout
        float codeHeight = contentHeight - 180; // Reserve space for display
        float halfWidth = (middleWidth - 30) / 2;
        
        ImGui::Text("Source / Assembly (synchronized)");
        ImGui::Separator();
        
        // Source panel (left)
        ImGui::BeginChild("SourceSide", ImVec2(halfWidth, codeHeight), true);
        renderSourcePanel(chip8);
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Disassembly panel (right)
        ImGui::BeginChild("DisasmSide", ImVec2(halfWidth, codeHeight), true);
        renderDisassembly(chip8);
        ImGui::EndChild();
    } else {
        renderDisassembly(chip8);
    }
    
    ImGui::Separator();
    renderDisplay(chip8);
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel: Memory editor + Watch + ROM browser
    ImGui::BeginChild("RightPanel", ImVec2(0, contentHeight), true);
    if (ImGui::BeginTabBar("RightTabs")) {
        if (ImGui::BeginTabItem("Memory")) {
            renderMemoryEditor(chip8);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Watch")) {
            renderWatchPanel(chip8);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ROM Browser")) {
            renderRomBrowser();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Decompiler")) {
            renderDecompiler(chip8);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    ImGui::End();

    // Render
    ImGui::Render();
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

void DebugUI::renderControls(Chip8& chip8) {
    // Execution controls
    if (paused) {
        if (ImGui::Button("Run [F5]")) {
            paused = false;
        }
    } else {
        if (ImGui::Button("Pause [F5]")) {
            paused = true;
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button("Step [F10]")) {
        step_requested = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Reset [F6]")) {
        chip8.reset();
        paused = true;
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Breakpoint input
    ImGui::SetNextItemWidth(60);
    ImGui::InputText("##bp", breakpoint_input, sizeof(breakpoint_input),
                     ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputTextWithHint("##bpcond", "condition", condition_input, sizeof(condition_input));
    ImGui::SameLine();
    if (ImGui::Button("Add BP")) {
        unsigned int addr;
        if (sscanf(breakpoint_input, "%x", &addr) == 1 && addr < 4096) {
            BreakpointInfo bp;
            bp.condition = condition_input;
            breakpoints[static_cast<uint16_t>(addr)] = bp;
            breakpoint_input[0] = '\0';
            condition_input[0] = '\0';
        }
    }

    // Show breakpoints
    if (!breakpoints.empty()) {
        ImGui::SameLine();
        ImGui::Text("BP:");
        for (auto it = breakpoints.begin(); it != breakpoints.end();) {
            ImGui::SameLine();
            char buf[32];
            if (it->second.condition.empty()) {
                snprintf(buf, sizeof(buf), "%03X", it->first);
            } else {
                snprintf(buf, sizeof(buf), "%03X?", it->first);  // ? indicates conditional
            }
            if (ImGui::SmallButton(buf)) {
                it = breakpoints.erase(it);
            } else {
                if (ImGui::IsItemHovered() && !it->second.condition.empty()) {
                    ImGui::SetTooltip("Condition: %s", it->second.condition.c_str());
                }
                ++it;
            }
        }
    }

    // Status
    ImGui::SameLine();
    float rightAlign = ImGui::GetWindowWidth() - 150;
    ImGui::SetCursorPosX(rightAlign);
    if (paused) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "PAUSED");
    } else {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "RUNNING");
    }

    // Keyboard shortcuts
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        paused = !paused;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F10) && paused) {
        step_requested = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F6)) {
        chip8.reset();
        paused = true;
    }
}

void DebugUI::renderMemoryEditor(Chip8& chip8) {
    ImGui::Text("Memory (4KB)");
    mem_editor.DrawContents(chip8.getMemory(), 4096);
}

void DebugUI::renderRegisters(Chip8& chip8) {
    ImGui::Text("Registers");
    ImGui::Separator();

    const uint8_t* V = chip8.getV();

    // V registers
    for (int i = 0; i < 16; i++) {
        ImGui::Text("V%X: %02X (%3d)", i, V[i], V[i]);
    }

    ImGui::Separator();

    // Special registers
    ImGui::TextColored(ImVec4(0.5f, 1, 0.5f, 1), "I:  %04X", chip8.getI());
    ImGui::TextColored(ImVec4(1, 1, 0.5f, 1), "PC: %04X", chip8.getPC());
    ImGui::Text("SP: %02X", chip8.getSP());

    ImGui::Separator();

    // Timers
    ImGui::Text("DT: %02X", chip8.delay_timer);
    ImGui::Text("ST: %02X", chip8.sound_timer);
}

void DebugUI::renderStack(Chip8& chip8) {
    ImGui::Text("Stack");
    ImGui::Separator();

    const uint16_t* stack = chip8.getStack();
    uint8_t sp = chip8.getSP();

    for (int i = 15; i >= 0; i--) {
        if (i == sp) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), ">%2d: %04X", i, stack[i]);
        } else if (i < sp) {
            ImGui::Text(" %2d: %04X", i, stack[i]);
        } else {
            ImGui::TextDisabled(" %2d: %04X", i, stack[i]);
        }
    }
}

void DebugUI::renderDisassembly(Chip8& chip8) {
    ImGui::Text("Disassembly");
    ImGui::Separator();

    uint16_t pc = chip8.getPC();
    const uint8_t* mem = chip8.getMemory();

    // Show instructions around current PC
    int start = std::max(0x200, static_cast<int>(pc) - 16);
    int end = std::min(4094, static_cast<int>(pc) + 32);

    // Align to 2-byte boundary
    start = start & ~1;
    
    // Get the line for sync highlighting from source view
    std::vector<uint16_t> sync_addresses;
    if (has_source_map && selected_source_line > 0 && highlight_from_source) {
        sync_addresses = source_map.getAddressesForLine(selected_source_line);
    }

    ImGui::BeginChild("DisasmScroll", ImVec2(0, 0), false);

    for (int addr = start; addr < end; addr += 2) {
        uint16_t opcode = (mem[addr] << 8) | mem[addr + 1];
        std::string dis = disassemble(opcode);

        bool is_breakpoint = breakpoints.count(addr) > 0;
        bool is_current = (addr == pc);
        
        // Check if this address is highlighted from source sync
        bool is_sync_highlighted = false;
        for (uint16_t sync_addr : sync_addresses) {
            if (addr == sync_addr) {
                is_sync_highlighted = true;
                break;
            }
        }

        // Marker column
        char marker = ' ';
        ImVec4 color = ImVec4(1, 1, 1, 1);

        if (is_current && is_breakpoint) {
            marker = '>';
            color = ImVec4(1, 0.5f, 0, 1);
        } else if (is_current) {
            marker = '>';
            color = ImVec4(1, 1, 0, 1);
        } else if (is_breakpoint) {
            marker = '*';
            color = ImVec4(1, 0.3f, 0.3f, 1);
        } else if (is_sync_highlighted) {
            // Cyan highlight for synchronized lines from source
            color = ImVec4(0.4f, 0.9f, 1.0f, 1);
        }

        ImGui::TextColored(color, "%c %03X: %04X %s", marker, addr, opcode, dis.c_str());

        // Scroll to current instruction
        if (is_current) {
            ImGui::SetScrollHereY(0.3f);
        }

        // Click to toggle breakpoint
        if (ImGui::IsItemClicked()) {
            if (is_breakpoint) {
                breakpoints.erase(addr);
            } else {
                breakpoints[addr] = BreakpointInfo();
            }
        }
        
        // Hover to sync with source panel
        if (has_source_map && ImGui::IsItemHovered()) {
            selected_asm_address = addr;
            highlight_from_source = false;
            ImGui::SetTooltip("Click to toggle breakpoint");
        }
    }

    ImGui::EndChild();
}

void DebugUI::renderDisplay(Chip8& chip8) {
    ImGui::Text("Display (64x32)");
    ImGui::Separator();

    const uint8_t* display = chip8.getDisplay();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    const float scale = 4.0f;
    const ImU32 on_color = IM_COL32(200, 255, 200, 255);
    const ImU32 off_color = IM_COL32(20, 40, 20, 255);

    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 64; x++) {
            ImU32 color = display[y * 64 + x] ? on_color : off_color;
            ImVec2 min(p.x + x * scale, p.y + y * scale);
            ImVec2 max(min.x + scale - 1, min.y + scale - 1);
            draw_list->AddRectFilled(min, max, color);
        }
    }

    ImGui::Dummy(ImVec2(64 * scale, 32 * scale));
}

bool DebugUI::loadSourceMap(const std::string& path) {
    if (source_map.load(path)) {
        has_source_map = true;
        return true;
    }
    return false;
}

void DebugUI::renderSourcePanel(Chip8& chip8) {
    if (!has_source_map) {
        ImGui::TextDisabled("No source map loaded");
        return;
    }
    
    // Get current source location
    uint16_t pc = chip8.getPC();
    int current_line = 0, current_col = 0;
    std::string current_file;
    source_map.getSourceLocation(pc, current_line, current_col, current_file);
    
    // Get sync highlighted line from disassembly hover
    int sync_line = 0;
    if (!highlight_from_source && selected_asm_address > 0) {
        sync_line = source_map.getLineForAddress(selected_asm_address);
    }
    
    ImGui::Text("Source");
    if (current_line > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(line %d)", current_line);
    }
    ImGui::Separator();
    
    ImGui::BeginChild("SourceScroll", ImVec2(0, 0), false);
    
    int line_count = source_map.getLineCount();
    for (int i = 1; i <= line_count; i++) {
        std::string line = source_map.getSourceLine(i);
        
        // Check if this line has a breakpoint
        uint16_t line_addr;
        bool has_addr = source_map.getAddress(i, line_addr);
        bool is_breakpoint = has_addr && breakpoints.count(line_addr) > 0;
        bool is_current = (i == current_line);
        bool is_sync_highlighted = (i == sync_line && sync_line > 0);
        
        // Marker and color
        char marker = ' ';
        ImVec4 color = ImVec4(0.7f, 0.7f, 0.7f, 1);
        
        if (is_current && is_breakpoint) {
            marker = '>';
            color = ImVec4(1, 0.5f, 0, 1);
        } else if (is_current) {
            marker = '>';
            color = ImVec4(1, 1, 0, 1);
        } else if (is_breakpoint) {
            marker = '*';
            color = ImVec4(1, 0.3f, 0.3f, 1);
        } else if (is_sync_highlighted) {
            // Cyan highlight for synchronized lines from disassembly
            color = ImVec4(0.4f, 0.9f, 1.0f, 1);
        } else if (has_addr) {
            color = ImVec4(1, 1, 1, 1);
        }
        
        ImGui::TextColored(color, "%c%4d| %s", marker, i, line.c_str());
        
        // Scroll to current line
        if (is_current) {
            ImGui::SetScrollHereY(0.3f);
        }
        
        // Click to toggle breakpoint on executable lines
        if (has_addr && ImGui::IsItemClicked()) {
            if (is_breakpoint) {
                breakpoints.erase(line_addr);
            } else {
                breakpoints[line_addr] = BreakpointInfo();
            }
        }
        
        // Hover to sync with disassembly panel
        if (has_addr && ImGui::IsItemHovered()) {
            selected_source_line = i;
            highlight_from_source = true;
            ImGui::SetTooltip("Click to toggle breakpoint (addr: %03X)", line_addr);
        }
    }
    
    ImGui::EndChild();
}

void DebugUI::renderWatchPanel(Chip8& chip8) {
    ImGui::Text("Watch Expressions");
    ImGui::Separator();
    
    // Add watch input
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##watch", watch_input, sizeof(watch_input));
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
        if (watch_input[0] != '\0') {
            WatchEntry entry;
            entry.expression = watch_input;
            entry.last_value = "?";
            watch_list.push_back(entry);
            watch_input[0] = '\0';
        }
    }
    
    ImGui::Separator();
    
    // Display watches
    ImGui::BeginChild("WatchList", ImVec2(0, 0), false);
    
    for (size_t i = 0; i < watch_list.size(); ) {
        auto& w = watch_list[i];
        w.last_value = evaluateWatch(w.expression, chip8);
        
        ImGui::Text("%s:", w.expression.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", w.last_value.c_str());
        
        ImGui::SameLine();
        char remove_id[32];
        snprintf(remove_id, sizeof(remove_id), "X##%zu", i);
        if (ImGui::SmallButton(remove_id)) {
            watch_list.erase(watch_list.begin() + i);
        } else {
            i++;
        }
    }
    
    if (watch_list.empty()) {
        ImGui::TextDisabled("No watches. Try: V0, collision, @400");
    }
    
    ImGui::EndChild();
}

std::string DebugUI::evaluateWatch(const std::string& expr, Chip8& chip8) {
    const uint8_t* V = chip8.getV();
    const uint8_t* mem = chip8.getMemory();
    
    // Register watch: V0-VF
    if (expr.size() == 2 && (expr[0] == 'V' || expr[0] == 'v')) {
        char c = expr[1];
        int reg = -1;
        if (c >= '0' && c <= '9') reg = c - '0';
        else if (c >= 'A' && c <= 'F') reg = 10 + (c - 'A');
        else if (c >= 'a' && c <= 'f') reg = 10 + (c - 'a');
        
        if (reg >= 0 && reg < 16) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%02X (%d)", V[reg], V[reg]);
            return buf;
        }
    }
    
    // Special registers
    if (expr == "I" || expr == "i") {
        char buf[16];
        snprintf(buf, sizeof(buf), "%04X", chip8.getI());
        return buf;
    }
    if (expr == "PC" || expr == "pc") {
        char buf[16];
        snprintf(buf, sizeof(buf), "%04X", chip8.getPC());
        return buf;
    }
    if (expr == "SP" || expr == "sp") {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02X", chip8.getSP());
        return buf;
    }
    if (expr == "DT" || expr == "dt" || expr == "timer") {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02X (%d)", chip8.delay_timer, chip8.delay_timer);
        return buf;
    }
    if (expr == "ST" || expr == "st") {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02X", chip8.sound_timer);
        return buf;
    }
    if (expr == "collision" || expr == "VF" || expr == "vf") {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", V[0xF]);
        return buf;
    }
    
    // Memory address: @NNN or @0xNNN
    if (expr.size() > 1 && expr[0] == '@') {
        std::string addr_str = expr.substr(1);
        try {
            unsigned long addr = std::stoul(addr_str, nullptr, 0);
            if (addr < 4096) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%02X (%d)", mem[addr], mem[addr]);
                return buf;
            }
        } catch (...) {}
    }
    
    return "?";
}

bool DebugUI::evaluateCondition(const std::string& cond, Chip8& chip8) {
    // Parse simple conditions like "V0 > 10", "VF == 1", "V1 != 0"
    // Format: <operand> <op> <value>
    
    const uint8_t* V = chip8.getV();
    
    // Tokenize
    std::string s = cond;
    size_t i = 0;
    
    // Skip whitespace
    while (i < s.size() && isspace(s[i])) i++;
    
    // Parse left operand (register)
    int left_val = 0;
    if (i + 1 < s.size() && (s[i] == 'V' || s[i] == 'v')) {
        char c = s[i + 1];
        int reg = -1;
        if (c >= '0' && c <= '9') reg = c - '0';
        else if (c >= 'A' && c <= 'F') reg = 10 + (c - 'A');
        else if (c >= 'a' && c <= 'f') reg = 10 + (c - 'a');
        
        if (reg >= 0 && reg < 16) {
            left_val = V[reg];
            i += 2;
        } else {
            return false;
        }
    } else if (i < s.size() && (s.substr(i, 2) == "DT" || s.substr(i, 2) == "dt")) {
        left_val = chip8.delay_timer;
        i += 2;
    } else if (i < s.size() && (s.substr(i, 2) == "ST" || s.substr(i, 2) == "st")) {
        left_val = chip8.sound_timer;
        i += 2;
    } else {
        return false;
    }
    
    // Skip whitespace
    while (i < s.size() && isspace(s[i])) i++;
    
    // Parse operator
    enum { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE } op;
    if (i + 1 < s.size() && s.substr(i, 2) == "==") {
        op = OP_EQ; i += 2;
    } else if (i + 1 < s.size() && s.substr(i, 2) == "!=") {
        op = OP_NE; i += 2;
    } else if (i + 1 < s.size() && s.substr(i, 2) == "<=") {
        op = OP_LE; i += 2;
    } else if (i + 1 < s.size() && s.substr(i, 2) == ">=") {
        op = OP_GE; i += 2;
    } else if (i < s.size() && s[i] == '<') {
        op = OP_LT; i += 1;
    } else if (i < s.size() && s[i] == '>') {
        op = OP_GT; i += 1;
    } else if (i < s.size() && s[i] == '=') {
        op = OP_EQ; i += 1;  // Single = also works
    } else {
        return false;
    }
    
    // Skip whitespace
    while (i < s.size() && isspace(s[i])) i++;
    
    // Parse right operand (number or register)
    int right_val = 0;
    if (i + 1 < s.size() && (s[i] == 'V' || s[i] == 'v')) {
        char c = s[i + 1];
        int reg = -1;
        if (c >= '0' && c <= '9') reg = c - '0';
        else if (c >= 'A' && c <= 'F') reg = 10 + (c - 'A');
        else if (c >= 'a' && c <= 'f') reg = 10 + (c - 'a');
        
        if (reg >= 0 && reg < 16) {
            right_val = V[reg];
        } else {
            return false;
        }
    } else {
        // Parse number (decimal or hex)
        std::string num_str = s.substr(i);
        try {
            right_val = std::stoi(num_str, nullptr, 0);
        } catch (...) {
            return false;
        }
    }
    
    // Evaluate
    switch (op) {
        case OP_EQ: return left_val == right_val;
        case OP_NE: return left_val != right_val;
        case OP_LT: return left_val < right_val;
        case OP_GT: return left_val > right_val;
        case OP_LE: return left_val <= right_val;
        case OP_GE: return left_val >= right_val;
    }
    
    return false;
}

std::vector<std::string> DebugUI::listRomFiles(const std::string& directory) {
    std::vector<std::string> entries;
    
    try {
        // Add parent directory option
        fs::path dir_path(directory);
        if (dir_path.has_parent_path()) {
            entries.push_back("..");
        }
        
        for (const auto& entry : fs::directory_iterator(directory)) {
            std::string name = entry.path().filename().string();
            if (entry.is_directory()) {
                entries.push_back("[" + name + "]");
            } else {
                // Filter ROM files
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".ch8" || ext == ".rom" || ext == ".c8" || ext == ".bin") {
                    entries.push_back(name);
                }
            }
        }
        
        std::sort(entries.begin(), entries.end());
    } catch (...) {
        // Directory access error
    }
    
    return entries;
}

void DebugUI::renderRomBrowser() {
    auto& config = chip8::getConfig();
    
    // Initialize browse directory
    if (current_browse_dir.empty()) {
        if (!config.last_directory.empty()) {
            current_browse_dir = config.last_directory;
        } else {
            current_browse_dir = fs::current_path().string();
        }
        directory_entries = listRomFiles(current_browse_dir);
    }
    
    // Recent ROMs section
    if (config.recent_count > 0) {
        ImGui::Text("Recent ROMs");
        ImGui::Separator();
        for (int i = 0; i < config.recent_count; i++) {
            fs::path rom_path(config.recent_roms[i]);
            std::string display_name = rom_path.filename().string();
            if (ImGui::Selectable(display_name.c_str())) {
                if (rom_load_callback && fs::exists(rom_path)) {
                    rom_load_callback(config.recent_roms[i]);
                    paused = true;
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", config.recent_roms[i].c_str());
            }
        }
        ImGui::Spacing();
    }
    
    // File browser
    ImGui::Text("Browse");
    ImGui::Separator();
    
    // Current path display
    ImGui::TextWrapped("%s", current_browse_dir.c_str());
    
    // Refresh button
    if (ImGui::Button("Refresh")) {
        directory_entries = listRomFiles(current_browse_dir);
    }
    
    ImGui::BeginChild("FileBrowser", ImVec2(0, 0), true);
    
    for (const auto& entry : directory_entries) {
        if (entry == "..") {
            if (ImGui::Selectable("[..]")) {
                fs::path parent = fs::path(current_browse_dir).parent_path();
                current_browse_dir = parent.string();
                directory_entries = listRomFiles(current_browse_dir);
            }
        } else if (entry.front() == '[' && entry.back() == ']') {
            // Directory
            if (ImGui::Selectable(entry.c_str())) {
                std::string dir_name = entry.substr(1, entry.size() - 2);
                current_browse_dir = (fs::path(current_browse_dir) / dir_name).string();
                directory_entries = listRomFiles(current_browse_dir);
            }
        } else {
            // ROM file
            if (ImGui::Selectable(entry.c_str())) {
                std::string full_path = (fs::path(current_browse_dir) / entry).string();
                if (rom_load_callback) {
                    config.addRecentRom(full_path);
                    config.last_directory = current_browse_dir;
                    config.saveDefault();
                    rom_load_callback(full_path);
                    paused = true;
                }
            }
        }
    }
    
    ImGui::EndChild();
}

std::string DebugUI::disassemble(uint16_t opcode) const {
    std::ostringstream oss;

    uint8_t x = (opcode & 0x0F00) >> 8;
    uint8_t y = (opcode & 0x00F0) >> 4;
    uint8_t n = opcode & 0x000F;
    uint8_t nn = opcode & 0x00FF;
    uint16_t nnn = opcode & 0x0FFF;

    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) oss << "CLS";
            else if (opcode == 0x00EE) oss << "RET";
            else oss << "SYS " << std::hex << nnn;
            break;
        case 0x1000: oss << "JP " << std::hex << std::uppercase << nnn; break;
        case 0x2000: oss << "CALL " << std::hex << std::uppercase << nnn; break;
        case 0x3000: oss << "SE V" << std::hex << std::uppercase << (int)x << ", " << (int)nn; break;
        case 0x4000: oss << "SNE V" << std::hex << std::uppercase << (int)x << ", " << (int)nn; break;
        case 0x5000: oss << "SE V" << std::hex << std::uppercase << (int)x << ", V" << (int)y; break;
        case 0x6000: oss << "LD V" << std::hex << std::uppercase << (int)x << ", " << (int)nn; break;
        case 0x7000: oss << "ADD V" << std::hex << std::uppercase << (int)x << ", " << (int)nn; break;
        case 0x8000:
            switch (n) {
                case 0x0: oss << "LD V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0x1: oss << "OR V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0x2: oss << "AND V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0x3: oss << "XOR V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0x4: oss << "ADD V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0x5: oss << "SUB V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0x6: oss << "SHR V" << std::hex << (int)x; break;
                case 0x7: oss << "SUBN V" << std::hex << (int)x << ", V" << (int)y; break;
                case 0xE: oss << "SHL V" << std::hex << (int)x; break;
                default: oss << "???"; break;
            }
            break;
        case 0x9000: oss << "SNE V" << std::hex << (int)x << ", V" << (int)y; break;
        case 0xA000: oss << "LD I, " << std::hex << std::uppercase << nnn; break;
        case 0xB000: oss << "JP V0, " << std::hex << std::uppercase << nnn; break;
        case 0xC000: oss << "RND V" << std::hex << (int)x << ", " << (int)nn; break;
        case 0xD000: oss << "DRW V" << std::hex << (int)x << ", V" << (int)y << ", " << (int)n; break;
        case 0xE000:
            if (nn == 0x9E) oss << "SKP V" << std::hex << (int)x;
            else if (nn == 0xA1) oss << "SKNP V" << std::hex << (int)x;
            else oss << "???";
            break;
        case 0xF000:
            switch (nn) {
                case 0x07: oss << "LD V" << std::hex << (int)x << ", DT"; break;
                case 0x0A: oss << "LD V" << std::hex << (int)x << ", K"; break;
                case 0x15: oss << "LD DT, V" << std::hex << (int)x; break;
                case 0x18: oss << "LD ST, V" << std::hex << (int)x; break;
                case 0x1E: oss << "ADD I, V" << std::hex << (int)x; break;
                case 0x29: oss << "LD F, V" << std::hex << (int)x; break;
                case 0x33: oss << "LD B, V" << std::hex << (int)x; break;
                case 0x55: oss << "LD [I], V" << std::hex << (int)x; break;
                case 0x65: oss << "LD V" << std::hex << (int)x << ", [I]"; break;
                default: oss << "???"; break;
            }
            break;
        default: oss << "???" ; break;
    }

    return oss.str();
}

void DebugUI::renderDecompiler(Chip8& chip8) {
    ImGui::Text("Pseudo-Code Decompiler");
    ImGui::Separator();
    
    // Refresh button
    if (ImGui::Button("Decompile ROM") || decompiler_needs_refresh) {
        // Get ROM from memory (starting at 0x200)
        const uint8_t* mem = chip8.getMemory();
        std::vector<uint8_t> rom;
        
        // Find end of ROM (look for long sequences of zeros)
        size_t rom_end = 0x200;
        for (size_t i = 0x200; i < 4096; i++) {
            if (mem[i] != 0) {
                rom_end = i + 1;
            }
        }
        
        // Copy ROM data
        for (size_t i = 0x200; i < rom_end; i++) {
            rom.push_back(mem[i]);
        }
        
        if (!rom.empty()) {
            decompiler.analyze(rom);
            decompiled_code = decompiler.decompile();
        } else {
            decompiled_code = "// No ROM loaded\n";
        }
        decompiler_needs_refresh = false;
    }
    
    ImGui::SameLine();
    if (!decompiled_code.empty()) {
        if (ImGui::Button("Copy to Clipboard")) {
            ImGui::SetClipboardText(decompiled_code.c_str());
        }
        ImGui::SameLine();
    }
    
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("Decompiles the current ROM into pseudo-code.");
        ImGui::Text("Features:");
        ImGui::BulletText("Function detection (CALL targets)");
        ImGui::BulletText("Loop detection (back edges)");
        ImGui::BulletText("Pattern matching for common idioms");
        ImGui::EndTooltip();
    }
    
    // Show analysis info
    auto& funcs = decompiler.getFunctions();
    auto& loops = decompiler.getLoops();
    if (!funcs.empty()) {
        ImGui::Text("Functions: %zu  Loops: %zu", funcs.size(), loops.size());
    }
    
    ImGui::Separator();
    
    // Decompiled code display
    ImGui::BeginChild("DecompiledCode", ImVec2(0, 0), true);
    
    if (!decompiled_code.empty()) {
        // Display with syntax highlighting
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        
        // Split into lines for display
        std::istringstream stream(decompiled_code);
        std::string line;
        while (std::getline(stream, line)) {
            // Color comments
            if (line.find("//") != std::string::npos || line.find("/*") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "%s", line.c_str());
            }
            // Color function declarations
            else if (line.find("void ") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s", line.c_str());
            }
            // Color control flow
            else if (line.find("if (") != std::string::npos || 
                     line.find("goto") != std::string::npos ||
                     line.find("return") != std::string::npos) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.5f, 1.0f), "%s", line.c_str());
            }
            // Color function calls
            else if (line.find("call ") != std::string::npos ||
                     line.find("draw(") != std::string::npos ||
                     line.find("clear(") != std::string::npos) {
                ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "%s", line.c_str());
            }
            else {
                ImGui::Text("%s", line.c_str());
            }
        }
        
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("Click 'Decompile ROM' to generate pseudo-code");
    }
    
    ImGui::EndChild();
}
