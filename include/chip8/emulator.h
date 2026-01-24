/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Core CHIP-8 emulator class
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>

class Chip8 {
public:
    static const std::array<uint8_t, 80> FONTSET;

    std::array<uint8_t, 4096> memory{};
    std::array<uint8_t, 16> V{};
    uint16_t I = 0;
    uint16_t PC = 0x200;
    std::array<uint16_t, 16> stack{};
    uint8_t SP = 0;
    uint8_t delay_timer = 0;
    uint8_t sound_timer = 0;

    Chip8();
    ~Chip8();

    void reset();
    void loadROM(const std::string& filename);
};
