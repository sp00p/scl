/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * CHIP-8 emulator implementation
 */

#include <chip8/emulator.h>
#include <chip8/keyboard_map.h>
#include <fstream>
#include <stdexcept>
#include <cstring>

const std::array<uint8_t, 80> Chip8::FONTSET = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,  // 0
    0x20, 0x60, 0x20, 0x20, 0x70,  // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0,  // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0,  // 3
    0x90, 0x90, 0xF0, 0x10, 0x10,  // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0,  // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0,  // 6
    0xF0, 0x10, 0x20, 0x40, 0x40,  // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0,  // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0,  // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90,  // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0,  // B
    0xF0, 0x80, 0x80, 0x80, 0xF0,  // C
    0xE0, 0x90, 0x90, 0x90, 0xE0,  // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0,  // E
    0xF0, 0x80, 0xF0, 0x80, 0x80   // F
};

Chip8::Chip8() : gen(rd()), dis(0, 255) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        throw std::runtime_error("SDL initialization failed: " + std::string(SDL_GetError()));
    }

    window = SDL_CreateWindow("CHIP-8 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        throw std::runtime_error("Window creation failed: " + std::string(SDL_GetError()));
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        throw std::runtime_error("Renderer creation failed: " + std::string(SDL_GetError()));
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture) {
        throw std::runtime_error("Texture creation failed: " + std::string(SDL_GetError()));
    }

    setupAudio();
    reset();
}

Chip8::~Chip8() {
    SDL_CloseAudioDevice(audio_device);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

void Chip8::reset() {
    memory.fill(0);
    V.fill(0);
    I = 0;
    PC = 0x200;
    stack.fill(0);
    SP = 0;
    delay_timer = 0;
    sound_timer = 0;
    display.fill(0);
    keys.fill(false);
    draw_flag = true;

    for (size_t i = 0; i < FONTSET.size(); ++i) {
        memory[i] = FONTSET[i];
    }
}

void Chip8::loadROM(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open ROM: " + filename);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > 4096 - 0x200) {
        throw std::runtime_error("ROM too large for memory");
    }

    file.read(reinterpret_cast<char*>(&memory[0x200]), size);
}

void Chip8::emulateCycle() {
    if (PC >= 4094) return;

    uint16_t opcode = (memory[PC] << 8) | memory[PC + 1];

    uint8_t x = (opcode & 0x0F00) >> 8;
    uint8_t y = (opcode & 0x00F0) >> 4;
    uint8_t n = opcode & 0x000F;
    uint8_t nn = opcode & 0x00FF;
    uint16_t nnn = opcode & 0x0FFF;

    PC += 2;

    switch (opcode & 0xF000) {
        case 0x0000:
            switch (opcode) {
                case 0x00E0:  // CLS
                    display.fill(0);
                    draw_flag = true;
                    break;
                case 0x00EE:  // RET
                    if (SP > 0) {
                        --SP;
                        PC = stack[SP];
                    }
                    break;
            }
            break;

        case 0x1000:  // JP addr
            PC = nnn;
            break;

        case 0x2000:  // CALL addr
            if (SP < 16) {
                stack[SP++] = PC;
                PC = nnn;
            }
            break;

        case 0x3000:  // SE Vx, byte
            if (V[x] == nn) PC += 2;
            break;

        case 0x4000:  // SNE Vx, byte
            if (V[x] != nn) PC += 2;
            break;

        case 0x5000:  // SE Vx, Vy
            if (V[x] == V[y]) PC += 2;
            break;

        case 0x6000:  // LD Vx, byte
            V[x] = nn;
            break;

        case 0x7000:  // ADD Vx, byte
            V[x] += nn;
            break;

        case 0x8000:
            switch (n) {
                case 0x0: V[x] = V[y]; break;
                case 0x1: V[x] |= V[y]; break;
                case 0x2: V[x] &= V[y]; break;
                case 0x3: V[x] ^= V[y]; break;
                case 0x4: {
                    uint16_t sum = V[x] + V[y];
                    V[0xF] = (sum > 255) ? 1 : 0;
                    V[x] = sum & 0xFF;
                    break;
                }
                case 0x5: {
                    V[0xF] = (V[x] >= V[y]) ? 1 : 0;
                    V[x] -= V[y];
                    break;
                }
                case 0x6: {
                    V[0xF] = V[x] & 0x1;
                    V[x] >>= 1;
                    break;
                }
                case 0x7: {
                    V[0xF] = (V[y] >= V[x]) ? 1 : 0;
                    V[x] = V[y] - V[x];
                    break;
                }
                case 0xE: {
                    V[0xF] = (V[x] & 0x80) >> 7;
                    V[x] <<= 1;
                    break;
                }
            }
            break;

        case 0x9000:  // SNE Vx, Vy
            if (V[x] != V[y]) PC += 2;
            break;

        case 0xA000:  // LD I, addr
            I = nnn;
            break;

        case 0xB000:  // JP V0, addr
            PC = nnn + V[0];
            break;

        case 0xC000:  // RND Vx, byte
            V[x] = dis(gen) & nn;
            break;

        case 0xD000: {  // DRW Vx, Vy, nibble
            V[0xF] = 0;
            int start_x = V[x] % SCREEN_WIDTH;
            int start_y = V[y] % SCREEN_HEIGHT;

            for (int row = 0; row < n; row++) {
                int pixel_y = start_y + row;
                if (pixel_y >= SCREEN_HEIGHT) break;

                uint8_t sprite_byte = memory[I + row];
                for (int col = 0; col < 8; col++) {
                    int pixel_x = start_x + col;
                    if (pixel_x >= SCREEN_WIDTH) break;

                    if ((sprite_byte & (0x80 >> col)) != 0) {
                        int idx = pixel_y * SCREEN_WIDTH + pixel_x;
                        if (display[idx] == 1) V[0xF] = 1;
                        display[idx] ^= 1;
                    }
                }
            }
            draw_flag = true;
            break;
        }

        case 0xE000:
            switch (nn) {
                case 0x9E:  // SKP Vx
                    if (V[x] < 16 && keys[V[x]]) PC += 2;
                    break;
                case 0xA1:  // SKNP Vx
                    if (V[x] >= 16 || !keys[V[x]]) PC += 2;
                    break;
            }
            break;

        case 0xF000:
            switch (nn) {
                case 0x07:  // LD Vx, DT
                    V[x] = delay_timer;
                    break;
                case 0x0A: {  // LD Vx, K
                    bool pressed = false;
                    for (int i = 0; i < 16; i++) {
                        if (keys[i]) {
                            V[x] = i;
                            pressed = true;
                            break;
                        }
                    }
                    if (!pressed) PC -= 2;
                    break;
                }
                case 0x15:  // LD DT, Vx
                    delay_timer = V[x];
                    break;
                case 0x18:  // LD ST, Vx
                    sound_timer = V[x];
                    break;
                case 0x1E:  // ADD I, Vx
                    I += V[x];
                    break;
                case 0x29:  // LD F, Vx
                    I = (V[x] & 0x0F) * 5;
                    break;
                case 0x33:  // LD B, Vx
                    if (I + 2 < 4096) {
                        memory[I] = V[x] / 100;
                        memory[I + 1] = (V[x] / 10) % 10;
                        memory[I + 2] = V[x] % 10;
                    }
                    break;
                case 0x55:  // LD [I], Vx
                    for (int i = 0; i <= x; i++) {
                        if (I + i < 4096) memory[I + i] = V[i];
                    }
                    break;
                case 0x65:  // LD Vx, [I]
                    for (int i = 0; i <= x; i++) {
                        if (I + i < 4096) V[i] = memory[I + i];
                    }
                    break;
            }
            break;
    }
}

void Chip8::handleInput(SDL_Event& event) {
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        bool pressed = (event.type == SDL_KEYDOWN);
        auto it = chip8::keyboard::KEY_MAP.find(event.key.keysym.sym);
        if (it != chip8::keyboard::KEY_MAP.end()) {
            keys[it->second] = pressed;
        }
    }
}

void Chip8::render() {
    if (draw_flag) {
        const uint32_t on = 0xFFFFFFFF;   // white
        const uint32_t off = 0x000000FF;  // black

        for (int y = 0; y < SCREEN_HEIGHT; ++y) {
            for (int x = 0; x < SCREEN_WIDTH; ++x) {
                pixel_buffer[y * SCREEN_WIDTH + x] = display[y * SCREEN_WIDTH + x] ? on : off;
            }
        }

        SDL_UpdateTexture(texture, nullptr, pixel_buffer.data(),
            SCREEN_WIDTH * static_cast<int>(sizeof(uint32_t)));
        draw_flag = false;
    }

    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
}

void Chip8::setupAudio() {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.userdata = this;
    want.callback = audioCallback;

    audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_device == 0) {
        throw std::runtime_error("Failed to open audio device: " + std::string(SDL_GetError()));
    }
    SDL_PauseAudioDevice(audio_device, 0);
}

void Chip8::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<Chip8*>(userdata);
    auto* buffer = reinterpret_cast<Sint16*>(stream);
    int samples = len / static_cast<int>(sizeof(Sint16));

    if (!self || self->sound_timer == 0) {
        std::memset(stream, 0, len);
        return;
    }

    const int sample_rate = 44100;
    const int freq = 440;  // 440 Hz square wave
    int period = sample_rate / freq;

    for (int i = 0; i < samples; ++i) {
        buffer[i] = ((self->audio_phase % period) < (period / 2)) ? 6000 : -6000;
        self->audio_phase++;
    }
}
