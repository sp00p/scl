/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Main entry point
 */

#include <chip8/emulator.h>
#include <chip8/compiler/compiler.h>
#include <chip8/disassembler.h>
#include <iostream>
#include <string>

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name << " run <rom_file>              - Run a CHIP-8 ROM\n"
              << "  " << program_name << " compile <source> <output>   - Compile SCL to CHIP-8\n"
              << "  " << program_name << " disasm <rom_file> [output]  - Disassemble a CHIP-8 ROM\n";
}

int run_compiler(const std::string& source_file, const std::string& output_file, bool debug) {
    try {
        chip8::compiler::Compiler compiler;
        compiler.setDebugMode(debug);
        compiler.compile(source_file, output_file);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << std::endl;
        return 1;
    }
}

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
        std::cerr << "Emulator error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "run") {
        if (argc < 3) {
            std::cerr << "Error: ROM file required for 'run' command\n";
            print_usage(argv[0]);
            return 1;
        }
        return run_emulator(argv[2]);
    } else if (command == "compile") {
        if (argc < 4) {
            std::cerr << "Error: source and output files required for 'compile' command\n";
            print_usage(argv[0]);
            return 1;
        }
        bool debug = (argc > 4 && std::string(argv[4]) == "--debug");
        return run_compiler(argv[2], argv[3], debug);
    } else if (command == "disasm") {
        if (argc < 3) {
            std::cerr << "Error: ROM file required for 'disasm' command\n";
            print_usage(argv[0]);
            return 1;
        }
        std::string output = (argc > 3) ? argv[3] : "-";
        return chip8::runDisassembler(argv[2], output);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        return 1;
    }
}
