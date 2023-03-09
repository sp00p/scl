#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#define MEMORY_SIZE 4096
#define REGISTERS_COUNT 16
#define STACK_SIZE 16
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32
#define NUM_KEYS 16
#define FONTSET_SIZE 80

uint8_t memory[MEMORY_SIZE];
uint8_t V[REGISTERS_COUNT];
uint16_t I;
uint16_t pc;
uint8_t delay_timer;
uint8_t sound_timer;
uint32_t last_sound_decrement_time;
uint32_t last_delay_decrement_time;
uint32_t last_cycle_time;
uint8_t sp;
uint8_t x;
uint8_t y;
uint8_t sprite_speed = 0;
uint16_t stack[STACK_SIZE];
uint8_t screen[SCREEN_WIDTH * SCREEN_HEIGHT];
uint8_t keys[NUM_KEYS];

SDL_Window* window;
SDL_Renderer* renderer;
Mix_Chunk *beep;

void print_mem();


int keymap[NUM_KEYS] = {
    SDLK_1, SDLK_2, SDLK_3, SDLK_4,
    SDLK_q, SDLK_w, SDLK_e, SDLK_r,
    SDLK_a, SDLK_s, SDLK_d, SDLK_f,
    SDLK_z, SDLK_x, SDLK_c, SDLK_v
};

const uint8_t fontset[FONTSET_SIZE] = {
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

void load_rom(const char* filename, bool print_memory, bool testing) {
    // open rom file
    FILE* file = fopen(filename, "rb");
    if (!file && testing == 0) {
        printf("Failed to load ROM file! Does it exist?\n");
        return;
    }

    // for testing
    // tests 1-3 work, no user input yet! :(
    //memory[0x1FF] = 1;

    // get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    // read file to memory
    fread(memory + 0x200, size, 1, file);

    // close file
    fclose(file);

    printf("Memory Initialized B)\n");

    if (print_memory) {
        print_mem();
    }


}

void run_tests(char* test_num) {
    uint8_t test = (uint8_t)atoi(test_num);
    printf("Test being run: %d\n", test);
    memory[0x1FF] = test;
    load_rom("../tests/chip8-test-suite.ch8", false, true);
    printf("\n\nRunning Tests....\n");
}

void print_mem() {
    printf("================\n");
    printf("     Memory     \n");
    printf("================\n");

    for (int i = 0; i < MEMORY_SIZE; i += 16) {
        printf("%04X: ", i);
        for (int j = 0; j < 16; j++) {
            if (i + j < MEMORY_SIZE) {
                printf("%02X ", memory[i + j]);
            } else {
                printf("   ");
            }
        }

        printf("| ");
        for (int j = 0; j < 16; j++) {
            if (i + j < MEMORY_SIZE && isprint(memory[i + j])) {
                printf("%c", memory[i + j]);
            } else {
                printf(".");
            }
        }

        printf("\n");
    }

}

int InitSDL() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("Failed to initialize SDL! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    // create window
    printf("Creating Window...\n");
    window = SDL_CreateWindow("SCL", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH * 10, SCREEN_HEIGHT * 10, SDL_WINDOW_SHOWN);

    if (!window) {
        printf("Failed to create window! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    printf("Done\n");

    // create renderer
    printf("Creating Renderer...\n");
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        printf("Failed to create renderer! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    printf("Done.\n");

    printf("Initializing audio...");
    if(Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("Failed to initialize SDL Mixer! SDL_Error: %s\n", SDL_GetError());
        return 1;
    } else {
        beep = Mix_LoadWAV("assets/beep.wav");
        if (!beep) {
            printf("Failed to find the beep.wav file. Does it exist?");
            return 1;
        }
        printf("Done.\n");
    }

    return 0;
}

void emulate() {

    // Sound handling
    uint32_t current_time = SDL_GetTicks();

    if(delay_timer > 0) {
        if(current_time - last_delay_decrement_time >= 1000 / 60) {
            delay_timer--;
            last_delay_decrement_time = current_time;
        }
    }

    if (sound_timer > 0) {
        if (current_time - last_sound_decrement_time >= 1000 / 60) {
            sound_timer--;
            last_sound_decrement_time = current_time;
            Mix_PlayChannel(-1, beep, 0); // play a beep sound
        }
    }



    uint16_t opcode = memory[pc] << 8 | memory[pc + 1];

    // decode opcode and execute instruction
    switch(opcode & 0xF000) {
        case 0x0000:
            switch(opcode & 0x00FF) {
                case 0x00E0: // CLS
                    memset(screen, 0, sizeof(screen));
                    pc += 2;
                    break;
                case 0x00EE: // RET
                    sp--;
                    pc = stack[sp];
                    pc += 2;
                    break;
                default:
                    //printf("not implemented yet\n");
                    pc += 2;
                    break;
                }
                break;
            case 0x1000: // JP addr
                pc = opcode & 0x0FFF;
                break;
            case 0x2000: // CALL addr
                stack[sp] = pc;
                sp++;
                pc = opcode & 0x0FFF;
                break;
            case 0x3000: // SE Vx, byte
                if (V[(opcode >> 8) & 0x0F] == (opcode & 0xFF)) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            case 0x4000: // SNE Vx, byte
                if (V[(opcode >> 8) & 0x0F] != (opcode & 0xFF)) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            case 0x5000: // SE Vx, Vy
                if (V[(opcode >> 8) & 0x0F] == V[(opcode >> 4) & 0x0F]) {
                    pc += 4;
                } else {
                    pc += 2;
                }
                break;
            case 0x6000: // LD Vx, byte
                V[(opcode >> 8) & 0x0F] = opcode & 0xFF;
                pc += 2;
                break;
            case 0x7000: // ADD Vx, byte
                V[(opcode >> 8) & 0x0F] += opcode & 0xFF;
                pc += 2;
                break;
            case 0x8000:
                switch(opcode & 0x000F) {
                    case 0x0000: //LD Vx, Vy
                        V[(opcode >> 8) & 0x0F] = V[(opcode >> 4) & 0x0F];
                        pc += 2;
                        break;
                    case 0x0001: // OR Vx, Vy
                        V[(opcode >> 8) & 0x0F] != V[(opcode >> 4) & 0x0F];
                        pc += 2;
                        break;
                    case 0x0002: // AND Vx
                        V[(opcode >> 8) & 0x0F] &= V[(opcode >> 4) & 0x0F];
                        pc += 2;
                        break;
                    case 0x0003: // XOR Vx, Vy
                        V[(opcode >> 8) & 0x0F] ^= V[(opcode >> 4) & 0x0F];
                        pc += 2;
                        break;
                    case 0x0004: // ADD Vx, Vy
                        if (V[(opcode >> 8) & 0x0F] + V[(opcode >> 4) & 0x0F] > 0xFF) {
                            V[0xF] = 1;
                        } else {
                            V[0xF] = 0;
                        }
                        V[(opcode >> 8) & 0x0F] += V[(opcode >> 4) & 0x0F];
                        pc += 2;
                        break;
                    case 0x0005: // SUB Vx, Vy
                        if (V[(opcode >> 8) & 0x0F] > V[(opcode >> 4) & 0x0F]) {
                            V[0xF] = 1;
                        } else {
                            V[0xF] = 0;
                        }
                        V[(opcode >> 8) & 0x0F] -= V[(opcode >> 4) & 0x0F];
                        pc += 2;
                        break;
                    case 0x0006: // SHR Bx {, Vy}
                        V[0xF] = V[(opcode >> 8) & 0x0F] & 0x1;
                        V[(opcode >> 8) & 0x0F] >>= 1;
                        pc += 2;
                        break;
                    case 0x0007: // SUBN Vx, Vy
                        if (V[(opcode >> 4) & 0x0F] > V[(opcode >> 8) & 0x0F]) {
                            V[0xF] = 1;
                        } else {
                            V[0xF] = 0;
                        }
                        V[(opcode >> 8) & 0x0F] -= V[(opcode >> 4) & 0x0F] - V[(opcode >> 8) & 0x0F];
                        pc += 2;
                        break;
                    case 0x000E: // SHL Vx {, Vy}
                        V[0xF] = V[(opcode >> 8) & 0x0F] >> 7;
                        V[(opcode >> 8) & 0x0F] <<= 1;
                        pc += 2;
                        break;
                    default:
                        //printf("not implemented (0x8000)\n");
                        break;
                    }
                    break;
                case 0x9000: // SNE Vx, Vy
                    if (V[(opcode >> 8) & 0xF0] != V[(opcode >> 4) & 0x0F]) {
                        pc += 4;
                    } else {
                        pc += 2;
                    }
                    break;
                case 0xA000: // LD I, addr
                    I = opcode & 0x0FFF;
                    pc += 2;
                    break;
                case 0xB000: // JP V0, addr
                    pc = (opcode & 0x0FFF) + V[0];
                    break;
                case 0xC000: // RND Vx, byte
                    V[(opcode >> 8) & 0x0F] = (rand() % 0xFF) & (opcode & 0xFF);
                    pc += 2;
                    break;
                case 0xD000: // DRW Vx, Vy, nibble
                    // draw sprite at (Vx, Vy) with width 8 pixels and height nibble pixels, set VF to collision
                    x = V[(opcode >> 8) & 0x0F];
                    y = V[(opcode >> 4) & 0x0F];
                    uint8_t height = opcode & 0x000F;
                    V[0xF] = 0;

                    for (int row = 0; row < height; row++) {
                        uint8_t sprite_byte = memory[I + row];
                        for (int col = 0; col < 8; col++) {
                            if ((sprite_byte & (0x80 >> col)) != 0) {
                                int index = (x + col) % SCREEN_WIDTH + ((y + row) % SCREEN_HEIGHT) * SCREEN_WIDTH;
                                if (screen[index] == 1) {
                                    V[0xF] = 1;
                                }
                                screen[index] ^= 1;
                            }
                        }
                    }
                    pc += 2;
                    break;
                case 0xE000:
                    switch(opcode & 0x00FF) {
                        case 0x009E: // SKP Vx
                            // skip next instruction if key with value of Vx is pressed
                            if (SDL_GetKeyboardState(NULL)[keymap[V[(opcode >> 8) & 0x0F]]]) {
                                pc += 4;
                            } else {
                                pc += 2;
                            }
                            break;
                        case 0x00A1: // SKNP Vx
                        // skip next instruction if key with value of Vx is not pressed
                        if (!SDL_GetKeyboardState(NULL)[keymap[V[(opcode >> 8) & 0x0F]]]) {
                            pc += 4;
                        } else {
                            pc += 2;
                        }
                        break;
                    default:
                        //printf("not implemented (0xE000)\n");
                        break;
                    }
                    break;
                case 0xF000:
                    switch (opcode & 0x00FF) {
                        case 0x0007: //LD Vx, DT
                        // load delay timer value into Vx
                        V[(opcode >> 8) & 0x0F] = delay_timer;
                        pc += 2;
                        break;
                case 0x000A: // LD Vx, K
                // wait for key press and store key value in Vx
                    for (int i = 0; i < 16; i++) {
                        if (SDL_GetKeyboardState(NULL)[keymap[i]]) {
                            V[(opcode >> 8) & 0x0F] = i;
                            pc += 2;
                            break;
                        }
                    }
                    break;
                case 0x0015: // LD DT, Vx
                // load Vx into delay timer
                    delay_timer = V[(opcode >> 8) & 0x0F];
                    pc += 2;
                    break;
                case 0x0018: // LD ST, Vx
                // load Vx into sound timer
                    sound_timer = V[(opcode >> 8) & 0x0F];
                    pc += 2;
                    break;
                case 0x001E: // ADD I, Vx
                    I += V[(opcode >> 8) & 0x0F];
                    pc += 2;
                    break;
                case 0x0029: // LD F, Vx
                // load font sprite for digit Vx into I
                    I = V[(opcode >> 8) & 0x0F] * 5;
                    pc += 2;
                    break;
                case 0x0033: // LD B, Vx
                // store binary-coded decimal representation of Vx at I, I+1, I+2
                    memory[I] = V[(opcode >> 8) & 0x0F] / 100;
                    memory[I + 1] = (V[(opcode >> 8) & 0x0F] / 10) % 10;
                    memory[I + 2] = V[(opcode >> 8) & 0x0F] % 10;
                    pc += 2;
                    break;
                case 0x0055: // LD [I], Vx
                // Store V0 through Vx at memory locations starting at I
                    for (int i = 0; i <= ((opcode >> 8) & 0x0F); i++) {
                        memory[I + i] = V[i];
                    }
                    pc += 2;
                    break;
                case 0x0065: // LD Vx, [I]
                // load V0 through Vx from memory locations starting at I
                    for (int i = 0; i <= ((opcode >> 8) & 0x0F); i++) {
                        V[i] = memory[I + i];
                    }
                    pc += 2;
                    break;
                default:
                    //printf("not implemented (0xF000)\n");
                    break;
                }
                break;
            default:
                //printf("not implemented\n");
                break;
    }
}

int main(int argc, char* argv[]) {
    if (InitSDL() != 0) {
        return 1;
    }

    if (argc < 2 || argc > 5) {
        printf("Usage: ./scl [-t] [1-5] [-p] <rom>");
        return 1;
    }

    if (strcmp(argv[1], "-p") == 0 && strcmp(argv[2], "-t") == 0) {
        printf("Usage ./scl [-t] [1-5] [-p] <rom>");
        return 1;
    }

    if (argc == 3 && strcmp(argv[1], "-t") == 0) {
        run_tests(argv[2]); // ./scl -t [1-5]
    } else if (argc == 2 && strcmp(argv[1], "-t") == 1 && strcmp(argv[1], "-p") == 1) { // ./scl <rom>
        load_rom(argv[1], false, false);
    } else if (argc == 3 && strcmp(argv[1], "-p") == 0) { // ./scl -p <rom>
        load_rom(argv[2], true, false);
    }else {
        printf("Usage ./scl [-t] [1-5] [-p] <rom>");
        return 1;
    }

    bool quit = false;

    while(!quit) {
        SDL_Event e;
        while(SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                quit = true;
            }
        }

        // emulate a single cycle of the cpu
        emulate();

        // render screen
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
            int x = i % SCREEN_WIDTH;
            int y = i / SCREEN_WIDTH;
            if (screen[i] == 1) {
                SDL_Rect rect = {x * 10, y * 10, 10, 10};
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                SDL_RenderFillRect(renderer, &rect);
            }
        }
        SDL_RenderPresent(renderer);

        uint32_t current_time = SDL_GetTicks();
        uint32_t cycle_duration = current_time - last_cycle_time;

        if (cycle_duration < (1000 / 60)) {
            SDL_Delay((1000 / 60) - cycle_duration);
        }

        last_cycle_time = current_time;
    }

    Mix_CloseAudio();
    Mix_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

