/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Main entry point
 */

#include <chip8/emulator.h>
#include <iostream>

int run_emulator(const std::string& rom_file) {
    try {
        Chip8 chip8;
        chip8.loadROM(rom_file);

        bool quit = false;
        SDL_Event event;
        Uint32 lastUpdateTime = SDL_GetTicks();
        const Uint32 MS_PER_UPDATE = 16;  // ~60 Hz
        const int INSTRUCTIONS_PER_UPDATE = 10;

        while (!quit) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    quit = true;
                }
                if (event.type == SDL_WINDOWEVENT && 
                    event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    quit = true;
                }
                chip8.handleInput(event);
            }

            Uint32 currentTime = SDL_GetTicks();
            if (currentTime - lastUpdateTime >= MS_PER_UPDATE) {
                if (chip8.delay_timer > 0) --chip8.delay_timer;
                if (chip8.sound_timer > 0) --chip8.sound_timer;

                for (int i = 0; i < INSTRUCTIONS_PER_UPDATE; i++) {
                    chip8.emulateCycle();
                }

                SDL_SetRenderDrawColor(chip8.getRenderer(), 0, 0, 0, 255);
                SDL_RenderClear(chip8.getRenderer());
                chip8.render();
                SDL_RenderPresent(chip8.getRenderer());

                lastUpdateTime = currentTime;
            }
            SDL_Delay(1);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <rom_file>" << std::endl;
        return 1;
    }

    return run_emulator(argv[1]);
}
