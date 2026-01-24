# Emulator

The SCL emulator runs CHIP-8 and Super CHIP-8 ROMs with full debugging support.

## Quick Start

```bash
# Run a ROM
scl run game.ch8

# Run with debugger
scl debug game.ch8

# Run in Super CHIP-8 mode (128x64)
scl run game.ch8 --schip

# Compile and run an SCL program
scl compile examples/snake.scl snake.ch8
scl run snake.ch8
```

## Keyboard

```
CHIP-8     Keyboard
1 2 3 C    1 2 3 4
4 5 6 D    Q W E R
7 8 9 E    A S D F
A 0 B F    Z X C V
```

Most games use WASD (keys 5/7/8/9) for movement and X (key 0) for action.

## Debugger

The debug window (opened with `debug` command) shows registers, stack, memory, and disassembly.

**Controls:**
- **F5** — Run/Pause
- **F10** — Step (single instruction)
- **F6** — Reset

**Breakpoints:**
- Enter a hex address (e.g., `204`) and click Add BP
- Conditional breakpoints: add a condition like `V0 > 10` or `VF == 1`
- Breakpoints pause execution when the PC reaches that address (and condition is met)

**Watch Expressions:**
- Monitor register values: `V0`, `V5`, `VF`
- Monitor memory: `[300]`, `[I]`
- Expressions: `V0 + V1`, `VF * 2`

**Source Mapping:**
When debugging compiled SCL programs, the disassembly shows the original source lines alongside the generated opcodes.

**ROM Browser:**
Browse and load ROMs directly from the debug window without restarting.

**Decompiler Panel:**
View pseudo-code reconstruction of the ROM with detected functions, loops, and control structures. Useful for understanding ROM behavior.

The decompiler is ROM-agnostic and works with any CHIP-8 ROM:
- Detects sprite data by tracing DRW instructions
- Recognizes array access patterns (LD I; ADD I; LD/ST)
- Identifies for-loop patterns with counter variables
- Infers variable names from usage context (x/y for coordinates)

## Quirks

CHIP-8 interpreters vary in behavior. This emulator defaults to original COSMAC VIP behavior:

- **VF Reset**: VF cleared before AND/OR/XOR
- **Shift**: 8XY6/8XYE shift VY, not VX
- **Index Increment**: FX55/FX65 increment I
- **Sprite Clipping**: Sprites clip at screen edge, don't wrap

These can be toggled in the config file.

## Memory

```
0x000-0x1FF  Reserved (fonts live here)
0x200-0xFFF  Program space (~3.5KB)
```

## Timing

~500 instructions/sec, 60Hz timers and display.

## Super CHIP-8

Pass `--schip` to enable 128x64 resolution, 16x16 sprites, scroll instructions, and RPL flag storage.

Additional opcodes: `00CN` (scroll down), `00FB/FC` (scroll right/left), `00FD` (exit), `00FE/FF` (lo/hi res), `DXY0` (16x16 sprite), `FX30` (large font), `FX75/85` (RPL flags).

## Instruction Reference

The emulator implements all 35 original CHIP-8 instructions plus SCHIP extensions.

### Standard CHIP-8 Opcodes

| Opcode | Description |
|--------|-------------|
| `00E0` | Clear screen |
| `00EE` | Return from subroutine |
| `1NNN` | Jump to address NNN |
| `2NNN` | Call subroutine at NNN |
| `3XNN` | Skip if VX == NN |
| `4XNN` | Skip if VX != NN |
| `5XY0` | Skip if VX == VY |
| `6XNN` | Set VX = NN |
| `7XNN` | Add VX += NN |
| `8XY0` | Set VX = VY |
| `8XY1` | Set VX = VX OR VY |
| `8XY2` | Set VX = VX AND VY |
| `8XY3` | Set VX = VX XOR VY |
| `8XY4` | Add VX += VY (VF = carry) |
| `8XY5` | Sub VX -= VY (VF = !borrow) |
| `8XY6` | Shift VX >>= 1 (VF = LSB) |
| `8XY7` | Sub VX = VY - VX (VF = !borrow) |
| `8XYE` | Shift VX <<= 1 (VF = MSB) |
| `9XY0` | Skip if VX != VY |
| `ANNN` | Set I = NNN |
| `BNNN` | Jump to V0 + NNN |
| `CXNN` | Random VX = rand() & NN |
| `DXYN` | Draw sprite at (VX, VY), height N |
| `EX9E` | Skip if key VX pressed |
| `EXA1` | Skip if key VX not pressed |
| `FX07` | Set VX = delay timer |
| `FX0A` | Wait for key, store in VX |
| `FX15` | Set delay timer = VX |
| `FX18` | Set sound timer = VX |
| `FX1E` | Add I += VX |
| `FX29` | Set I = font sprite for VX |
| `FX33` | Store BCD of VX at I |
| `FX55` | Store V0-VX at I |
| `FX65` | Load V0-VX from I |

### SCHIP Extensions

| Opcode | Description |
|--------|-------------|
| `00CN` | Scroll down N lines |
| `00FB` | Scroll right 4 pixels |
| `00FC` | Scroll left 4 pixels |
| `00FD` | Exit interpreter |
| `00FE` | Low resolution (64x32) |
| `00FF` | High resolution (128x64) |
| `DXY0` | Draw 16x16 sprite |
| `FX30` | Set I = large font for VX |
| `FX75` | Store V0-VX in RPL flags |
| `FX85` | Load V0-VX from RPL flags |

For more details, see [Cowgod's CHIP-8 Technical Reference](http://devernay.free.fr/hacks/chip8/C8TECH10.HTM).
