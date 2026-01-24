/*
 * SCL - CHIP-8 Emulator, Debugger, and Compiler
 * Copyright (c) 2026 Sean Cornell <secornell@ucsd.edu>
 * MIT License
 *
 * Global configuration constants for emulator timing.
 */

#pragma once

namespace chip8 {
namespace config {

constexpr int UPDATES_PER_SECOND = 60;
constexpr int MS_PER_UPDATE = 1000 / UPDATES_PER_SECOND;
constexpr int INSTRUCTIONS_PER_UPDATE = 10;

}
}
