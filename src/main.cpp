/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Main entry point
 */

#include <chip8/emulator.h>
#include <chip8/debug_ui.h>
#include <chip8/compiler/compiler.h>
#include <chip8/disassembler.h>
#include <chip8/decompiler.h>
#include <chip8/runtime_config.h>
#include <chip8/config.h>
#include <iostream>
#include <fstream>
#include <string>

void print_usage(const char* program_name) {
    std::cerr << "Usage:\n"
              << "  " << program_name << " run <rom_file> [--schip]    - Run a CHIP-8 ROM\n"
              << "  " << program_name << " debug <rom_file> [--schip]  - Run with debug UI\n"
              << "  " << program_name << " compile <src> <out> [opts]  - Compile SCL to CHIP-8\n"
              << "        options: --debug --stats --listing -O0\n"
              << "  " << program_name << " disasm <rom_file> [output]  - Disassemble a CHIP-8 ROM\n"
              << "  " << program_name << " decompile <rom_file>        - Decompile ROM to pseudo-code\n";
}

struct CompileOptions {
    bool debug = false;
    bool optimize = true;
    bool stats = false;
    bool listing = false;
};

int run_compiler(const std::string& source_file, const std::string& output_file,
                 const CompileOptions& opts) {
    try {
        chip8::compiler::Compiler compiler;
        compiler.setDebugMode(opts.debug);
        compiler.setOptimize(opts.optimize);
        compiler.setStats(opts.stats);
        compiler.setListing(opts.listing);
        compiler.compile(source_file, output_file);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << std::endl;
        return 1;
    }
}

int run_decompiler(const std::string& rom_file) {
    try {
        std::ifstream file(rom_file, std::ios::binary);
        if (!file) {
            std::cerr << "Error: Could not open ROM file: " << rom_file << std::endl;
            return 1;
        }
        
        std::vector<uint8_t> rom((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        file.close();
        
        chip8::Decompiler decompiler;
        decompiler.analyze(rom);
        std::cout << decompiler.decompile();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Decompilation error: " << e.what() << std::endl;
        return 1;
    }
}

int run_emulator(const std::string& rom_file, bool debug_mode, bool schip_mode = false) {
    try {
        Chip8 chip8;
        chip8.setSchipMode(schip_mode);
        chip8.loadROM(rom_file);
        
        // Add ROM to recent list and save config
        chip8::getConfig().addRecentRom(rom_file);
        chip8::getConfig().saveDefault();

        // Initialize debug UI if in debug mode
        DebugUI debug_ui;
        if (debug_mode) {
            if (!debug_ui.init()) {
                std::cerr << "Failed to initialize debug UI" << std::endl;
                return 1;
            }

            // Auto-load the source map emitted by the compiler, if present
            if (debug_ui.loadSourceMap(rom_file + ".map")) {
                std::cout << "Loaded source map: " << rom_file << ".map" << std::endl;
            }
            
            // Wire up ROM browser callback
            debug_ui.setRomLoadCallback([&chip8](const std::string& path) {
                try {
                    chip8.reset();
                    chip8.loadROM(path);
                    chip8::getConfig().addRecentRom(path);
                    chip8::getConfig().saveDefault();
                } catch (const std::exception& e) {
                    std::cerr << "Failed to load ROM: " << e.what() << std::endl;
                }
            });
        }

        bool quit = false;
        SDL_Event event;
        Uint32 lastUpdateTime = SDL_GetTicks();
        Uint32 emuWindowID = SDL_GetWindowID(chip8.getWindow());

        while (!quit) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    quit = true;
                }
                if (event.type == SDL_WINDOWEVENT &&
                    event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    if (event.window.windowID == emuWindowID) {
                        quit = true;
                    } else if (debug_mode && event.window.windowID == debug_ui.getWindowID()) {
                        quit = true;
                    }
                }
                
                // Let debug UI process events for its window
                if (debug_mode) {
                    debug_ui.processEvent(event);
                }
                
                chip8.handleInput(event);
            }

            Uint32 currentTime = SDL_GetTicks();
            if (currentTime - lastUpdateTime >= chip8::config::MS_PER_UPDATE) {
                if (chip8.delay_timer > 0) --chip8.delay_timer;
                if (chip8.sound_timer > 0) --chip8.sound_timer;

                // Only execute if not paused (or step requested)
                bool should_execute = !debug_mode || !debug_ui.isPaused() || debug_ui.shouldStep();
                
                if (should_execute) {
                    // New 60Hz frame: release a pending display wait
                    chip8.clearVblankWait();

                    int cycles = (debug_mode && debug_ui.isPaused()) ? 1
                        : chip8::getConfig().timing.instructions_per_frame;
                    for (int i = 0; i < cycles; i++) {
                        chip8.emulateCycle();

                        // SCHIP EXIT opcode (00FD) requests emulator shutdown
                        if (chip8.shouldExit()) {
                            quit = true;
                            break;
                        }

                        // Display wait quirk: original CHIP-8 draws at most one
                        // sprite per vblank. Stop executing until the next frame.
                        if (chip8::getConfig().quirks.display_wait &&
                            chip8.isWaitingForVblank()) {
                            break;
                        }

                        // Check for breakpoints after each instruction
                        if (debug_mode && debug_ui.isBreakpoint(chip8.getPC(), &chip8)) {
                            debug_ui.pause();
                            break;
                        }
                    }
                }

                SDL_SetRenderDrawColor(chip8.getRenderer(), 0, 0, 0, 255);
                SDL_RenderClear(chip8.getRenderer());
                chip8.render();
                SDL_RenderPresent(chip8.getRenderer());
                
                // Update debug window
                if (debug_mode) {
                    debug_ui.update(chip8);
                }

                lastUpdateTime = currentTime;
            }
            SDL_Delay(1);
        }
        
        if (debug_mode) {
            debug_ui.shutdown();
        }
    } catch (const std::exception& e) {
        std::cerr << "Emulator error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    // Load configuration
    chip8::getConfig().loadDefault();
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "run" || command == "debug") {
        if (argc < 3) {
            std::cerr << "Error: ROM file required for '" << command << "' command\n";
            print_usage(argv[0]);
            return 1;
        }
        bool schip = false;
        for (int i = 3; i < argc; i++) {
            if (std::string(argv[i]) == "--schip") {
                schip = true;
            } else {
                std::cerr << "Unknown option: " << argv[i] << "\n";
                print_usage(argv[0]);
                return 1;
            }
        }
        return run_emulator(argv[2], command == "debug", schip);
    } else if (command == "compile") {
        if (argc < 4) {
            std::cerr << "Error: source and output files required for 'compile' command\n";
            print_usage(argv[0]);
            return 1;
        }
        CompileOptions opts;
        for (int i = 4; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--debug")        opts.debug = true;
            else if (arg == "-O0")       opts.optimize = false;
            else if (arg == "--stats")   opts.stats = true;
            else if (arg == "--listing") opts.listing = true;
            else {
                std::cerr << "Unknown option: " << arg << "\n";
                print_usage(argv[0]);
                return 1;
            }
        }
        return run_compiler(argv[2], argv[3], opts);
    } else if (command == "disasm") {
        if (argc < 3) {
            std::cerr << "Error: ROM file required for 'disasm' command\n";
            print_usage(argv[0]);
            return 1;
        }
        std::string output = (argc > 3) ? argv[3] : "-";
        return chip8::runDisassembler(argv[2], output);
    } else if (command == "decompile") {
        if (argc < 3) {
            std::cerr << "Error: ROM file required for 'decompile' command\n";
            print_usage(argv[0]);
            return 1;
        }
        return run_decompiler(argv[2]);
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        return 1;
    }
}
