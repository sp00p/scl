/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * CHIP-8 and Super CHIP-8 emulator core. Handles CPU emulation,
 * display rendering, audio output, and input processing.
 */

#pragma once

#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include <random>
#include <string>

class Chip8 {
public:
    static constexpr int LORES_WIDTH = 64;
    static constexpr int LORES_HEIGHT = 32;
    static constexpr int HIRES_WIDTH = 128;
    static constexpr int HIRES_HEIGHT = 64;
    static constexpr int SCREEN_WIDTH = HIRES_WIDTH;
    static constexpr int SCREEN_HEIGHT = HIRES_HEIGHT;
    static constexpr int SCALE = 8;
    static constexpr int WINDOW_WIDTH = SCREEN_WIDTH * SCALE;
    static constexpr int WINDOW_HEIGHT = SCREEN_HEIGHT * SCALE;

    Chip8();
    ~Chip8();

    void reset();
    void loadROM(const std::string& filename);
    void emulateCycle();
    void handleInput(SDL_Event& event);
    void render();

    uint8_t* getMemory() { return memory.data(); }
    const uint8_t* getMemory() const { return memory.data(); }
    const uint8_t* getV() const { return V.data(); }
    uint16_t getI() const { return I; }
    uint16_t getPC() const { return PC; }
    uint8_t getSP() const { return SP; }
    const uint16_t* getStack() const { return stack.data(); }
    const uint8_t* getDisplay() const { return display.data(); }
    SDL_Window* getWindow() { return window; }
    SDL_Renderer* getRenderer() { return renderer; }

    bool debug_mode = false;
    uint8_t delay_timer;
    uint8_t sound_timer;
    SDL_AudioDeviceID audio_device;

    bool schip_mode = false;
    bool hires_mode = false;
    bool exit_requested = false;
    std::array<uint8_t, 8> rpl_flags;

    bool waiting_for_vblank = false;
    void clearVblankWait() { waiting_for_vblank = false; }
    bool isWaitingForVblank() const { return waiting_for_vblank; }

    int getDisplayWidth() const { return hires_mode ? HIRES_WIDTH : LORES_WIDTH; }
    int getDisplayHeight() const { return hires_mode ? HIRES_HEIGHT : LORES_HEIGHT; }
    bool isHiRes() const { return hires_mode; }
    void setSchipMode(bool enabled) { schip_mode = enabled; }
    bool shouldExit() const { return exit_requested; }

private:
    std::array<uint8_t, 4096> memory;
    std::array<uint8_t, 16> V;
    uint16_t I;
    uint16_t PC;
    std::array<uint16_t, 16> stack;
    uint8_t SP;
    int audio_phase = 0;

    std::array<uint8_t, SCREEN_WIDTH * SCREEN_HEIGHT> display;
    bool draw_flag;

    std::random_device rd;
    std::mt19937 gen;
    std::uniform_int_distribution<> dis;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    std::array<uint32_t, SCREEN_WIDTH * SCREEN_HEIGHT> pixel_buffer;
    std::array<bool, 16> keys;
    std::string current_rom;

    void debugPrint(const std::string& msg) const;
    void setupAudio();
    static void audioCallback(void* userData, Uint8* stream, int len);

    static const std::array<uint8_t, 80> FONTSET;
};
