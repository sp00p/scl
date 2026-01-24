/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Core CHIP-8 emulator class
 */

#pragma once

#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include <random>
#include <string>

class Chip8 {
public:
    static constexpr int SCREEN_WIDTH = 64;
    static constexpr int SCREEN_HEIGHT = 32;
    static constexpr int SCALE = 10;
    static constexpr int WINDOW_WIDTH = SCREEN_WIDTH * SCALE;
    static constexpr int WINDOW_HEIGHT = SCREEN_HEIGHT * SCALE;
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
    void emulateCycle();
    void handleInput(SDL_Event& event);
    void render();

    SDL_Window* getWindow() { return window; }
    SDL_Renderer* getRenderer() { return renderer; }

    // Accessors for debug UI
    uint16_t getPC() const { return PC; }
    uint16_t getI() const { return I; }
    uint8_t getSP() const { return SP; }
    const uint8_t* getV() const { return V.data(); }
    const uint8_t* getMemory() const { return memory.data(); }
    uint8_t* getMemory() { return memory.data(); }
    const uint16_t* getStack() const { return stack.data(); }
    const uint8_t* getDisplay() const { return display.data(); }

private:
    std::array<uint8_t, SCREEN_WIDTH * SCREEN_HEIGHT> display{};
    std::array<bool, 16> keys{};
    bool draw_flag = false;

    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<> dis;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    std::array<uint32_t, SCREEN_WIDTH * SCREEN_HEIGHT> pixel_buffer{};

    SDL_AudioDeviceID audio_device = 0;
    int audio_phase = 0;

    void setupAudio();
    static void audioCallback(void* userdata, Uint8* stream, int len);
};
