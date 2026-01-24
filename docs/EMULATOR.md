# Emulator

```bash
scl run game.ch8          # normal mode
scl debug game.ch8        # with debugger
scl run game.ch8 --schip  # Super CHIP-8 mode
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

See [Cowgod's CHIP-8 Technical Reference](http://devernay.free.fr/hacks/chip8/C8TECH10.HTM) for the full opcode list. The emulator implements all 35 original instructions plus SCHIP extensions.
