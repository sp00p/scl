#include <cstring>
#include <chip8/emulator.h>
#include <chip8/keyboard_map.h>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <iomanip>

const std::array<uint8_t, 80> Chip8::FONTSET = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
        0x20, 0x60, 0x20, 0x20, 0x70, // 1
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

// Super CHIP-8 10x10 large font (stored at 0x50 after small font)
static const std::array<uint8_t, 160> LARGE_FONTSET = {
    // 0
    0x3C, 0x7E, 0xE7, 0xC3, 0xC3, 0xC3, 0xC3, 0xE7, 0x7E, 0x3C,
    // 1
    0x18, 0x38, 0x58, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C,
    // 2
    0x3E, 0x7F, 0xC3, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFF, 0xFF,
    // 3
    0x3C, 0x7E, 0xC3, 0x03, 0x0E, 0x0E, 0x03, 0xC3, 0x7E, 0x3C,
    // 4
    0x06, 0x0E, 0x1E, 0x36, 0x66, 0xC6, 0xFF, 0xFF, 0x06, 0x06,
    // 5
    0xFF, 0xFF, 0xC0, 0xC0, 0xFC, 0xFE, 0x03, 0xC3, 0x7E, 0x3C,
    // 6
    0x3E, 0x7C, 0xC0, 0xC0, 0xFC, 0xFE, 0xC3, 0xC3, 0x7E, 0x3C,
    // 7
    0xFF, 0xFF, 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x60,
    // 8
    0x3C, 0x7E, 0xC3, 0xC3, 0x7E, 0x7E, 0xC3, 0xC3, 0x7E, 0x3C,
    // 9
    0x3C, 0x7E, 0xC3, 0xC3, 0x7F, 0x3F, 0x03, 0x03, 0x3E, 0x7C,
    // A
    0x18, 0x3C, 0x66, 0xC3, 0xC3, 0xFF, 0xFF, 0xC3, 0xC3, 0xC3,
    // B
    0xFC, 0xFE, 0xC3, 0xC3, 0xFC, 0xFC, 0xC3, 0xC3, 0xFE, 0xFC,
    // C
    0x3E, 0x7F, 0xC3, 0xC0, 0xC0, 0xC0, 0xC0, 0xC3, 0x7F, 0x3E,
    // D
    0xFC, 0xFE, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xFE, 0xFC,
    // E
    0xFF, 0xFF, 0xC0, 0xC0, 0xFC, 0xFC, 0xC0, 0xC0, 0xFF, 0xFF,
    // F
    0xFF, 0xFF, 0xC0, 0xC0, 0xFC, 0xFC, 0xC0, 0xC0, 0xC0, 0xC0
};

Chip8::Chip8()
        : gen(rd()), dis(0, 255), window(nullptr), renderer(nullptr), draw_flag(false) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        throw std::runtime_error("SDL Initialization failed: " + std::string(SDL_GetError()));
    }

    window = SDL_CreateWindow("CHIP-8 Emulator",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      WINDOW_WIDTH, WINDOW_HEIGHT,
      SDL_WINDOW_SHOWN);

    if (!window) {
        throw std::runtime_error("Window creation failed: " + std::string(SDL_GetError()));
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        throw std::runtime_error("Renderer creation failed: " + std::string(SDL_GetError()));
    }

    // Create streaming texture for fast scaled rendering
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                SCREEN_WIDTH,
                                SCREEN_HEIGHT);
    if (!texture) {
        throw std::runtime_error("Texture creation failed: " + std::string(SDL_GetError()));
    }

    setupAudio();
    reset();
}

Chip8::~Chip8() {
    SDL_CloseAudioDevice(audio_device);
    if (texture) SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
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
    hires_mode = false;  // Start in low-res mode
    exit_requested = false;
    rpl_flags.fill(0);

    // Load small font at 0x00
    for (size_t i = 0; i < FONTSET.size(); ++i) {
        memory[i] = FONTSET[i];
    }
    // Load large font at 0x50 (after small font)
    for (size_t i = 0; i < LARGE_FONTSET.size(); ++i) {
        memory[0x50 + i] = LARGE_FONTSET[i];
    }

    // Reload ROM if one was loaded
    if (!current_rom.empty()) {
        std::ifstream file(current_rom, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(&memory[0x200]), size);
        }
    }
}

void Chip8::loadROM(const std::string &filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open ROM file: " + filename);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size > 4096 - 0x200) {
        throw std::runtime_error("ROM too large for memory");
    }

    file.read(reinterpret_cast<char*>(&memory[0x200]), size);
    current_rom = filename;  // Store for reset
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
    SDL_PauseAudioDevice(audio_device, 0); // start device
}

void Chip8::audioCallback(void *userData, Uint8 *stream, int len) {
    auto* self = static_cast<Chip8*>(userData);
    auto* buffer = reinterpret_cast<Sint16*>(stream);
    int samples = len / static_cast<int>(sizeof(Sint16));

    if (!self || self->sound_timer == 0) {
        std::memset(stream, 0, len);
        return;
    }

    const int sample_rate = 44100;
    const int freq = 440; // Hz, square wave
    int period = sample_rate / freq;

    for (int i = 0; i < samples; ++i) {
        buffer[i] = ((self->audio_phase % period) < (period / 2)) ? 6000 : -6000;
        self->audio_phase++;
    }
}

void Chip8::debugPrint(const std::string &msg) const {
    if (debug_mode) {
        std::cout << msg << std::endl;
    }
}

void Chip8::emulateCycle() {
    // Bounds check for PC
    if (PC >= 4094) {
        return; // Halt execution if PC is out of bounds
    }
    uint16_t opcode = (memory[PC] << 8) | memory[PC + 1];

    if (debug_mode) {
        std::cout << "0x" << std::hex << PC << " : "
                  << std::setw(4) << std::setfill('0') << opcode
                  << std::dec << std::endl;
    }

    uint8_t x = (opcode & 0x0F00) >> 8; // second nibble
    uint8_t y = (opcode & 0x00F0) >> 4; // third nibble
    uint8_t n = opcode & 0x000F;        // fourth nibble
    uint8_t nn = opcode & 0x00FF;       // last byte
    uint16_t nnn = opcode & 0x0FFF;     // last 12 bits

    PC += 2;

    switch (opcode & 0xF000) {
        case 0x0000:
            if ((opcode & 0xFFF0) == 0x00C0 && schip_mode) {
                // 00CN: Scroll down N pixels (SCHIP)
                int n_scroll = opcode & 0x000F;
                int width = getDisplayWidth();
                int height = getDisplayHeight();
                for (int row = height - 1; row >= n_scroll; --row) {
                    for (int col = 0; col < width; ++col) {
                        display[row * SCREEN_WIDTH + col] = display[(row - n_scroll) * SCREEN_WIDTH + col];
                    }
                }
                for (int row = 0; row < n_scroll; ++row) {
                    for (int col = 0; col < width; ++col) {
                        display[row * SCREEN_WIDTH + col] = 0;
                    }
                }
                draw_flag = true;
                break;
            }
            switch (opcode) {
                case 0x00E0: // CLS
                    debugPrint("CLS");
                    display.fill(0);
                    draw_flag = true;
                    break;
                case 0x00EE: // RET
                    debugPrint("RET");
                    if (SP > 0) {
                        --SP;
                        PC = stack[SP];
                    }
                    break;
                case 0x00FB: // Scroll right 4 pixels (SCHIP)
                    if (schip_mode) {
                        int width = getDisplayWidth();
                        int height = getDisplayHeight();
                        for (int row = 0; row < height; ++row) {
                            for (int col = width - 1; col >= 4; --col) {
                                display[row * SCREEN_WIDTH + col] = display[row * SCREEN_WIDTH + col - 4];
                            }
                            for (int col = 0; col < 4; ++col) {
                                display[row * SCREEN_WIDTH + col] = 0;
                            }
                        }
                        draw_flag = true;
                    }
                    break;
                case 0x00FC: // Scroll left 4 pixels (SCHIP)
                    if (schip_mode) {
                        int width = getDisplayWidth();
                        int height = getDisplayHeight();
                        for (int row = 0; row < height; ++row) {
                            for (int col = 0; col < width - 4; ++col) {
                                display[row * SCREEN_WIDTH + col] = display[row * SCREEN_WIDTH + col + 4];
                            }
                            for (int col = width - 4; col < width; ++col) {
                                display[row * SCREEN_WIDTH + col] = 0;
                            }
                        }
                        draw_flag = true;
                    }
                    break;
                case 0x00FD: // EXIT (SCHIP)
                    if (schip_mode) {
                        exit_requested = true;
                    }
                    break;
                case 0x00FE: // LOW - Disable high-res mode (SCHIP)
                    if (schip_mode) {
                        hires_mode = false;
                        display.fill(0);
                        draw_flag = true;
                    }
                    break;
                case 0x00FF: // HIGH - Enable high-res mode (SCHIP)
                    if (schip_mode) {
                        hires_mode = true;
                        display.fill(0);
                        draw_flag = true;
                    }
                    break;
            }
            break;

        case 0x1000: // JP addr
            debugPrint("JP");
            PC = nnn;
            break;
        case 0x2000: // CALL addr
            debugPrint("CALL " + std::to_string(nnn));
            if (SP < 16) {
                stack[SP++] = PC;
                PC = nnn;
                break;
            }
            break;

        case 0x3000: // SE Vx
            if (V[x] == nn)
                PC += 2;
            break;
        case 0x4000: // SNE Vx
            if (V[x] != nn)
                PC += 2;
            break;
        case 0x5000: // SE Vx, Vy
            if (V[x] == V[y])
                PC += 2;
            break;
        case 0x6000: // LD Vx, byte
            V[x] = nn;
            break;
        case 0x7000: // ADD Vx, byte
            V[x] += nn;
            break;
        case 0x8000:
            switch(n) {
                case 0x0: //LD Vx, Vy
                    V[x] = V[y];
                    break;
                case 0x1: // OR Vx, Vy
                    V[x] |= V[y];
                    if (!schip_mode) V[0xF] = 0;  // VF reset quirk (original CHIP-8 only)
                    break;
                case 0x2: // AND Vx, Vy
                    V[x] &= V[y];
                    if (!schip_mode) V[0xF] = 0;  // VF reset quirk (original CHIP-8 only)
                    break;
                case 0x3: // XOR Vx, Vy
                    V[x] ^= V[y];
                    if (!schip_mode) V[0xF] = 0;  // VF reset quirk (original CHIP-8 only)
                    break;
                case 0x4: // ADD Vx, Vy
                {
                    // Buffer operands first (important if x or y is 0xF)
                    uint8_t vx = V[x];
                    uint8_t vy = V[y];
                    uint16_t sum = vx + vy;
                    V[x] = sum & 0xFF;
                    V[0xF] = (sum > 0xFF) ? 1 : 0;  // VF set LAST
                }
                    break;
                case 0x5: // SUB Vx, Vy
                {
                    uint8_t vx = V[x];
                    uint8_t vy = V[y];
                    uint8_t flag = (vx >= vy) ? 1 : 0;
                    V[x] = vx - vy;
                    V[0xF] = flag;  // VF set LAST
                }
                    break;
                case 0x6: // SHR Vx {, Vy}
                {
                    // CHIP-8: uses VY (quirk ON = shift VY, store in VX)
                    // SCHIP: uses VX (quirk OFF = shift VX directly)
                    uint8_t val = schip_mode ? V[x] : V[y];
                    uint8_t flag = val & 0x1;
                    V[x] = val >> 1;
                    V[0xF] = flag;  // VF set LAST
                }
                    break;
                case 0x7: // SUBN Vx, Vy
                {
                    uint8_t vx = V[x];
                    uint8_t vy = V[y];
                    uint8_t flag = (vy >= vx) ? 1 : 0;
                    V[x] = vy - vx;
                    V[0xF] = flag;  // VF set LAST
                }
                    break;
                case 0xE: // SHL Vx {, Vy}
                {
                    // CHIP-8: uses VY (quirk ON = shift VY, store in VX)
                    // SCHIP: uses VX (quirk OFF = shift VX directly)
                    uint8_t val = schip_mode ? V[x] : V[y];
                    uint8_t flag = (val & 0x80) >> 7;
                    V[x] = val << 1;
                    V[0xF] = flag;  // VF set LAST
                }
                    break;
            }
            break;
        case 0x9000: // SNE Vx, Vy
            if (V[x] != V[y])
                PC += 2;
            break;
        case 0xA000: // LD I, addr
            I = nnn;
            break;
        case 0xB000: // JP V0, addr (or JP Vx, addr in SCHIP)
            debugPrint("JP V0, " + std::to_string(nnn));
            // Original CHIP-8: jump to nnn + V0
            // SCHIP: jump to xnn + Vx (where x is first nibble of address)
            if (schip_mode) {
                PC = nnn + V[x];
            } else {
                PC = nnn + V[0];
            }
            break;
        case 0xC000: //RND Vx, byte
            V[x] = dis(gen) & nn;
            break;
        case 0xD000: // DRW Vx, Vy, nibble
        {
            // Display wait quirk: CHIP-8 waits for vblank before drawing
            // SCHIP does not wait (in high-res mode, or modern SCHIP in any mode)
            if (!schip_mode) {
                waiting_for_vblank = true;
            }
            
            V[0xF] = 0;
            
            int disp_width = getDisplayWidth();
            int disp_height = getDisplayHeight();
            int start_x = V[x] % disp_width;
            int start_y = V[y] % disp_height;
            
            // SCHIP: DXY0 draws a 16x16 sprite
            if (n == 0 && schip_mode) {
                for (int row = 0; row < 16; row++) {
                    int pixel_y = start_y + row;
                    if (pixel_y >= disp_height) break;
                    
                    // 16-bit wide sprite (2 bytes per row)
                    uint16_t mem_idx = I + row * 2;
                    if (mem_idx + 1 >= 4096) break;
                    uint16_t sprite_row = (memory[mem_idx] << 8) | memory[mem_idx + 1];
                    for (int col = 0; col < 16; col++) {
                        int pixel_x = start_x + col;
                        if (pixel_x >= disp_width) break;
                        
                        if ((sprite_row & (0x8000 >> col)) != 0) {
                            int pixel_pos = pixel_y * SCREEN_WIDTH + pixel_x;
                            if (display[pixel_pos] == 1) V[0xF] = 1;
                            display[pixel_pos] ^= 1;
                        }
                    }
                }
            } else {
                // Standard 8xN sprite
                for (int row = 0; row < n; row++) {
                    int pixel_y = start_y + row;
                    if (pixel_y >= disp_height) break;

                    if (I + row >= 4096) break;
                    uint8_t sprite_byte = memory[I + row];
                    debugPrint("  row " + std::to_string(row) + ": sprite_byte=0x" + std::to_string(sprite_byte));
                    for (int col = 0; col < 8; col++) {
                        int pixel_x = start_x + col;
                        if (pixel_x >= disp_width) break;

                        if ((sprite_byte & (0x80 >> col)) != 0) {
                            int pixel_pos = pixel_y * SCREEN_WIDTH + pixel_x;
                            if (display[pixel_pos] == 1) V[0xF] = 1;
                            display[pixel_pos] ^= 1;
                        }
                    }
                }
            }
            draw_flag = true;
        }
            break;
        case 0xE000:
            switch (nn) {
                case 0x9E: // SKP Vx
                    if (V[x] < 16 && keys[V[x]])
                        PC += 2;
                    break;
                case 0xA1: // SKNP Vx
                    if (V[x] >= 16 || !keys[V[x]])
                        PC += 2;
                    break;
            }
            break;
        case 0xF000:
            switch (nn) {
                case 0x07: // LD Vx, DT
                    V[x] = delay_timer;
                    break;

                case 0x0A: // LD Vx, K
                {
                    bool key_pressed = false;
                    for (int i = 0; i < 16; i++) {
                        if (keys[i]) {
                            V[x] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    if (!key_pressed)
                        PC -= 2;
                }
                    break;

                case 0x15: //LD DT, Vx
                    delay_timer = V[x];
                    break;
                case 0x18: // LD ST, Vx
                    sound_timer = V[x];
                    break;
                case 0x1E: // ADD I, Vx
                    I += V[x];
                    break;
                case 0x29: // LD F, Vx (small font)
                    I = (V[x] & 0x0F) * 5;
                    break;
                case 0x30: // LD HF, Vx (large font, SCHIP)
                    if (schip_mode) {
                        I = 0x50 + (V[x] & 0x0F) * 10;
                    }
                    break;
                case 0x33: // LD B, Vx
                    if (I + 2 < 4096) {
                        memory[I] = V[x] / 100;
                        memory[I + 1] = (V[x] / 10) % 10;
                        memory[I + 2] = V[x] % 10;
                    }
                    break;
                case 0x55: // LD [I], Vx - Store V0..Vx in memory
                    // Original CHIP-8: I increments after each store
                    // SCHIP: I remains unchanged
                    for (int i = 0; i <= x; i++) {
                        if (I + i < 4096) memory[I + i] = V[i];
                    }
                    if (!schip_mode) I += x + 1;
                    break;
                case 0x65: // LD Vx, [I] - Load V0..Vx from memory
                    // Original CHIP-8: I increments after each load
                    // SCHIP: I remains unchanged
                    for (int i = 0; i <= x; i++) {
                        if (I + i < 4096) V[i] = memory[I + i];
                    }
                    if (!schip_mode) I += x + 1;
                    break;
                case 0x75: // LD R, Vx - Store V0..Vx in RPL flags (SCHIP)
                    if (schip_mode) {
                        for (int i = 0; i <= x && i < 8; i++) {
                            rpl_flags[i] = V[i];
                        }
                    }
                    break;
                case 0x85: // LD Vx, R - Load V0..Vx from RPL flags (SCHIP)
                    if (schip_mode) {
                        for (int i = 0; i <= x && i < 8; i++) {
                            V[i] = rpl_flags[i];
                        }
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
    // Only update texture when display changed
    if (draw_flag) {
        const uint32_t on = 0xFFFFFFFFu;  // white (RGBA)
        const uint32_t off = 0x000000FFu; // black (RGBA)
        
        // In low-res mode, we need to scale up 64x32 to fill the 128x64 texture
        if (!hires_mode) {
            for (int y = 0; y < LORES_HEIGHT; ++y) {
                for (int x = 0; x < LORES_WIDTH; ++x) {
                    uint32_t color = display[y * SCREEN_WIDTH + x] ? on : off;
                    // Scale 2x in each dimension
                    pixel_buffer[(y * 2) * SCREEN_WIDTH + (x * 2)] = color;
                    pixel_buffer[(y * 2) * SCREEN_WIDTH + (x * 2 + 1)] = color;
                    pixel_buffer[(y * 2 + 1) * SCREEN_WIDTH + (x * 2)] = color;
                    pixel_buffer[(y * 2 + 1) * SCREEN_WIDTH + (x * 2 + 1)] = color;
                }
            }
        } else {
            // High-res: 1:1 mapping
            for (int y = 0; y < SCREEN_HEIGHT; ++y) {
                for (int x = 0; x < SCREEN_WIDTH; ++x) {
                    pixel_buffer[y * SCREEN_WIDTH + x] = display[y * SCREEN_WIDTH + x] ? on : off;
                }
            }
        }
        SDL_UpdateTexture(texture, nullptr, pixel_buffer.data(), SCREEN_WIDTH * static_cast<int>(sizeof(uint32_t)));
        draw_flag = false;
    }

    // Always render the texture
    SDL_Rect dest{0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderCopy(renderer, texture, nullptr, &dest);
}
