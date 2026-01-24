/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Runtime configuration for emulator quirks, display colors,
 * key mapping, timing, and persistent settings.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace chip8 {

struct Quirks {
    bool vf_reset = true;
    bool shift_vy = true;
    bool index_increment = true;
    bool jump_vx = false;
    bool clip_sprites = true;
    bool schip_mode = false;
};

struct Colors {
    uint32_t background = 0x1A1A2EFF;
    uint32_t foreground = 0x16C60CFF;
};

struct KeyMapping {
    std::array<int, 16> keys = {
        45, 30, 31, 32, 20, 26, 8, 4, 22, 7, 29, 6, 33, 21, 9, 25
    };
};

struct Timing {
    int instructions_per_frame = 16;
    int target_fps = 60;
};

struct RuntimeConfig {
    Quirks quirks;
    Colors colors;
    KeyMapping keys;
    Timing timing;
    std::array<std::string, 10> recent_roms;
    int recent_count = 0;
    std::string last_directory;

    bool load(const std::string& filepath);
    bool save(const std::string& filepath) const;
    bool loadDefault();
    bool saveDefault() const;
    void addRecentRom(const std::string& path);
    static std::string getDefaultConfigPath();
};

RuntimeConfig& getConfig();

}
