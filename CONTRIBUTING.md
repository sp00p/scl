# Contributing

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

Requirements: CMake 3.16+, C++17 compiler, SDL2.

## Running Tests

```bash
./build/Release/scl_tests
```

All tests must pass before submitting changes.

## Code Style

- C++17, no exceptions in hot paths
- 4-space indentation
- Braces on same line
- Keep functions short and focused

## Project Structure

```
src/
  compiler/       # SCL lexer, parser, codegen
  emulator/       # CHIP-8 interpreter, display, audio
  config/         # Runtime configuration
include/chip8/    # Public headers
tests/            # Unit and integration tests
games/            # Example SCL programs
docs/             # Documentation
```

## Adding Features

1. Write tests first when possible
2. Update documentation if adding user-facing features
3. Test with real ROMs and SCL programs

## Reporting Issues

Include:
- What you expected vs what happened
- Steps to reproduce
- SCL source or ROM file if applicable
- OS and compiler version
