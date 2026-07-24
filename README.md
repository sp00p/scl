# SCL

CHIP-8 emulator, debugger, and compiler. Write games in a C-like language and run them on a fully-featured emulator with step-through debugging.

## What's Included

**Emulator** — Runs CHIP-8 and Super CHIP-8 ROMs with configurable quirks for maximum compatibility. Includes sound, 60Hz timing, and customizable key bindings.

**Debugger** — ImGui-based debug window with conditional breakpoints, watch expressions, memory editor, register view, live disassembly, stack inspection, and source mapping.

**Compiler** — The SCL language compiles to CHIP-8 bytecode. Supports functions, arrays, control flow, inline assembly, and generates source maps for debugging.

**Disassembler** — Converts ROM files back to readable assembly with basic code flow analysis.

**Decompiler** — Analyzes ROM structure and generates pseudo-code with detected functions, loops, and control flow.

## Building

Requires CMake 3.16+ and a C++17 compiler. SDL2 and other dependencies are included as submodules.

### Quick Setup

**Linux/macOS:**
```bash
git clone --recurse-submodules https://github.com/user/scl.git
cd scl
./scripts/setup.sh
```

**Windows (PowerShell):**
```powershell
git clone --recurse-submodules https://github.com/user/scl.git
cd scl
.\scripts\setup.ps1
```

### Manual Build

```bash
git clone --recurse-submodules https://github.com/user/scl.git
cd scl
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Development Scripts

| Script | Description |
|--------|-------------|
| `scripts/setup.sh` / `setup.ps1` | Install dependencies and build |
| `scripts/build.sh` / `build.ps1` | Build the project |
| `scripts/test.sh` / `test.ps1` | Run tests |

Build options:
```bash
./scripts/build.sh debug    # Debug build
./scripts/build.sh release  # Release build (default)
./scripts/build.sh clean    # Clean and rebuild
```

## Quick Start

```bash
# Run a ROM
scl run tetris.ch8

# Debug a ROM (opens debugger window)
scl debug tetris.ch8

# Compile SCL source to ROM
scl compile game.scl game.ch8

# Disassemble a ROM
scl disasm game.ch8

# Decompile a ROM to pseudo-code
scl decompile game.ch8

# Enable Super CHIP-8 mode (128x64, scroll ops, 16x16 sprites)
scl run schip_game.ch8 --schip
```

## Keyboard

```
CHIP-8     Keyboard
1 2 3 C    1 2 3 4
4 5 6 D    Q W E R
7 8 9 E    A S D F
A 0 B F    Z X C V
```

Debugger: **F5** run/pause, **F6** reset, **F10** step.

## Example

A simple program that moves a sprite with WASD:

```c
sprite ball[3] = { 0xE0, 0xE0, 0xE0 };

void main() {
    byte x = 32;
    byte y = 16;
    
    while (1) {
        clear;
        if (key(5)) { y = y - 1; }  // W
        if (key(8)) { y = y + 1; }  // S
        if (key(7)) { x = x - 1; }  // A
        if (key(9)) { x = x + 1; }  // D
        draw(x, y, 3, ball);
        wait(16);
    }
}
```

See [docs/SCL.md](docs/SCL.md) for the full language reference.

## Examples

The `examples/` folder contains ready-to-run SCL programs:

- **hello.scl** — Simple sprite drawing
- **animation.scl** — Frame-based animation with movement
- **pong.scl** — Single-player Pong with functions
- **snake.scl** — Snake game with arrays and collision
- **breakout.scl** — Brick-breaking game
- **tilemap.scl** — Level rendering from const ROM data tables

Compile and run:
```bash
scl compile examples/snake.scl snake.ch8
scl run snake.ch8
```

## Configuration

Emulator settings (quirks, key bindings, colors, emulation speed) persist in
`~/.config/scl/config.ini` (Linux/macOS) or `%APPDATA%\scl\config.ini`
(Windows). See [docs/EMULATOR.md](docs/EMULATOR.md#configuration) for the
available options.

## Tests

Four GoogleTest suites cover the lexer, parser, code generator, and an
end-to-end integration suite that compiles SCL programs and executes them on
the emulator core:

```bash
cd build && ctest
# or run individually: ./test_lexer ./test_parser ./test_codegen ./test_integration
```

The integration tests run headless (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`).

## Docs

- [SCL Language Reference](docs/SCL.md)
- [Emulator Details](docs/EMULATOR.md)

## License

MIT
