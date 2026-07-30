# SCL Language Reference

SCL (Simple CHIP-8 Language) is a C-like language that compiles to CHIP-8 bytecode.

## Quick Start

```bash
# Compile an SCL file to a CHIP-8 ROM
# (also writes game.ch8.map, a source map used by the debugger)
scl compile game.scl game.ch8

# Run the compiled ROM
scl run game.ch8

# Run with the debugger; auto-loads game.ch8.map so you can step
# through and set breakpoints in your original SCL source
scl debug game.ch8
```

## Quick Reference

```c
// Types
byte x = 10;              // 8-bit variable (0-255)
byte arr[10];             // Array
global byte score;        // Global variable
const MAX = 100;          // Constant

// Sprites
sprite ball[3] = { 0xE0, 0xE0, 0xE0 };

// Read-only data tables (stored in ROM)
const byte levels[] = { 1, 0, 2, 1 };
byte t = levels[i];       // Indexed read

// Functions
void foo() { }            // No return value
byte bar(byte a) { return a + 1; }

// Control flow
if (x > 10) { } else { }
while (x < 100) { x++; }
for (i = 0; i < 10; i++) { }
switch (dir) { case 0: y--; case 1: y++; }

// Built-ins
clear;                    // Clear screen
draw(x, y, 3, sprite);    // Draw sprite
drawnum(score, 0, 0);     // Draw number
key(5)                    // Check key (returns 0/1)
waitkey()                 // Wait for key press
wait(16);                 // Delay in ms (rounded to 16ms timer ticks)
beep(10);                 // Play sound
rand(0xFF)                // Random 0-255
collision                 // 1 if last draw collided
```

## Program Structure

Programs consist of declarations (sprites, constants, enums, entities) followed by functions. The entry point is `main()`.

```c
// Constants and sprites first
const SPEED = 2;
sprite ball[3] = { 0xE0, 0xE0, 0xE0 };

// Helper functions
byte clamp(byte val, byte max) {
    if (val > max) { return max; }
    return val;
}

// Entry point
void main() {
    byte x = 32;
    byte y = 16;
    while (1) {
        clear;
        draw(x, y, 3, ball);
        if (key(9)) { x = x + SPEED; }
        wait(16);
    }
}
```

## Data Types

### byte
The primary data type is `byte`, an 8-bit unsigned integer (0-255).

```c
byte x;           // Declaration (uninitialized)
byte y = 10;      // Declaration with initialization
byte z = 0xFF;    // Hex literal (255)
```

### Arrays
Arrays store multiple bytes in memory (not registers).

```c
byte data[10];           // Declare array of 10 bytes
byte grid[8][8];         // 2D array (8x8)
data[0] = 42;            // Write to element
byte val = data[5];      // Read from element

// Variable indices
byte i = 3;
data[i] = 100;
grid[i][i] = 255;
```

### Character Literals
String literals evaluate to the ASCII value of their first character. This is useful for character comparisons:

```c
byte key_pressed = waitkey();
if (key_pressed == "A") {   // Compares against ASCII value 65
    // Handle 'A' key
}

byte initial = "X";         // Sets initial to 88 (ASCII for 'X')
```

Supported escape sequences: `\n` (newline), `\r` (carriage return), `\t` (tab), `\\` (backslash), `\"` (quote), `\0` (null).

## Variables

Local variables live in CHIP-8's V-registers, assigned by the compiler's
register allocator. There is no hard variable limit: when a function keeps
more than 13 values live at once, the extra ones are automatically spilled
to memory and reloaded as needed (register access is faster, so heavily
used values stay in registers).

```c
byte x = 10;     // Allocates a register
byte y = x + 5;  // y = 15
x = 20;          // Reassignment
```

### Scope
Variables declared inside a function are local to that function. When calling another function, the caller's registers are automatically saved and restored.

### Global Variables
For state shared between functions, use globals (stored in memory):

```c
global byte high_score;
global byte level = 1;

void main() {
    high_score = 100;
}
```

## Operators

### Arithmetic
| Operator | Description | Example |
|----------|-------------|---------|
| `+` | Addition | `x + y` |
| `-` | Subtraction | `x - y` |
| `*` | Multiplication | `x * y` |
| `/` | Integer Division | `x / y` |
| `%` | Modulo (remainder) | `x % 8` |
| `-` (unary) | Negate (two's complement) | `-x` |
| `++` | Increment | `x++` |
| `--` | Decrement | `x--` |

Note: All arithmetic wraps at 8 bits (0-255). Division is integer division.
Multiplication, division, and modulo by powers of two compile to fast
shifts/masks; multiplication by any constant uses shift-add sequences.
Dividing or taking modulo by a runtime value of zero hangs the program
(constant zero is a compile error).

### Compound Assignment
| Operator | Description | Equivalent |
|----------|-------------|------------|
| `+=` | Add assign | `x = x + y` |
| `-=` | Subtract assign | `x = x - y` |
| `*=` | Multiply assign | `x = x * y` |
| `/=` | Divide assign | `x = x / y` |
| `&=` | AND assign | `x = x & y` |
| `\|=` | OR assign | `x = x \| y` |
| `^=` | XOR assign | `x = x ^ y` |

### Bitwise
| Operator | Description | Example |
|----------|-------------|---------|
| `&` | AND | `x & 0x0F` |
| `\|` | OR | `x \| 0x80` |
| `^` | XOR | `x ^ y` |
| `<<` | Shift left | `x << 3` |
| `>>` | Shift right | `x >> 1` |

### Comparison
| Operator | Description |
|----------|-------------|
| `==` | Equal |
| `!=` | Not equal |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |

### Logical
| Operator | Description | Example |
|----------|-------------|---------|
| `!` | Logical NOT | `!flag` |
| `&&` | Logical AND | `a && b` |
| `\|\|` | Logical OR | `a \|\| b` |

### Precedence and Parentheses

Operators follow C-style precedence, highest first:

1. `!`, unary `-`, function calls, array indexing, `( )`
2. `*` `/` `%`
3. `+` `-`
4. `<<` `>>`
5. `&`
6. `^`
7. `\|`
8. `==` `!=` `<` `>` `<=` `>=`
9. `&&` `\|\|`

Use parentheses to group subexpressions:

```c
byte x = (i & 7) * 8 + 2;     // mask, then multiply, then add
if ((a < b && c) || done) { } // parens work in conditions too
```

## Control Flow

### if / else
```c
if (x > 10) {
    // executed if x > 10
}

if (x == 0) {
    // executed if x == 0
} else {
    // executed otherwise
}
```

### while
```c
while (x < 100) {
    x = x + 1;
}

// Infinite loop
while (1) {
    // game loop
}
```

### for
```c
byte i;
for (i = 0; i < 10; i = i + 1) {
    // executed 10 times
}
```

### switch
```c
switch (direction) {
    case 0: y = y - 1;   // up
    case 1: y = y + 1;   // down
    case 2: x = x - 1;   // left
    case 3: x = x + 1;   // right
    default: beep(1);    // unknown direction
}
```

Note: Cases fall through by default (no `break` in switch). Use `default` for unmatched values.

### break / continue
```c
while (1) {
    if (done) {
        break;      // Exit loop
    }
    if (skip) {
        continue;   // Skip to next iteration
    }
}
```

## Functions

### Void Functions
```c
void draw_player(byte x, byte y) {
    draw(x, y, 8, player_sprite);
}

void main() {
    draw_player(10, 20);
}
```

### Functions with Return Values
```c
byte add(byte a, byte b) {
    return a + b;
}

byte max(byte a, byte b) {
    if (a > b) { return a; }
    return b;
}

void main() {
    byte sum = add(5, 3);    // sum = 8
    byte m = max(10, 20);    // m = 20
}
```

### Parameters
Functions can take up to 14 parameters. Parameters are passed by value (copies).

```c
void example(byte a, byte b, byte c) {
    // a, b, c are local copies
    a = 100;  // Does not affect caller's variables
}
```

## Constants and Enums

### Constants
```c
const MAX_SPEED = 5;
const SCREEN_WIDTH = 64;
const SCREEN_HEIGHT = 32;

void main() {
    byte speed = MAX_SPEED;
}
```

### Const Data Tables

Read-only byte tables stored directly in ROM - no RAM or initialization
code needed. Ideal for level layouts, animation sequences, and lookup tables:

```c
const byte level[] = {
    1, 1, 1, 1,
    1, 0, 0, 1,
    1, 1, 1, 1
};

const byte sine[8] = { 0, 3, 5, 6, 6, 5, 3, 0 };  // size checked if given

void main() {
    byte i = 5;
    byte tile = level[i];        // Indexed read
    byte s = sine[i % 8];        // Any expression as index
}
```

Tables are one-dimensional and read-only (`level[0] = 5;` is a compile
error). Declare them at the top level before use. There are no runtime
bounds checks - an out-of-range index reads adjacent ROM bytes.

See `examples/tilemap.scl` for a complete level-rendering example.

### Enums
```c
enum Direction { UP, DOWN, LEFT, RIGHT }
enum State { IDLE = 0, RUNNING = 1, JUMPING = 2 }

void main() {
    byte dir = Direction.RIGHT;  // or just: RIGHT
    byte state = RUNNING;
}
```

## Entities

Entities are struct-like types for grouping related data. They're stored in memory.

### Defining Entities
```c
entity Player {
    byte x;
    byte y;
    byte health;
}

entity Bullet {
    byte x;
    byte y;
    byte active;
}
```

### Declaring Entity Variables
```c
Player player;         // Single entity
Bullet bullets[10];    // Array of 10 entities
```

### Accessing Fields
```c
void main() {
    Player player;
    player.x = 32;
    player.y = 16;
    player.health = 100;
    
    Bullet bullets[5];
    byte i;
    for (i = 0; i < 5; i++) {
        bullets[i].x = 0;
        bullets[i].y = 0;
        bullets[i].active = 0;
    }
    
    // Use in expressions
    if (player.health > 0) {
        draw(player.x, player.y, sprite);
    }
}
```

## Sprites

Sprites are 8 pixels wide and 1-15 pixels tall. Each row is one byte where bits represent pixels.

### Defining Sprites with Hex Values
```c
// 3x3 ball
sprite ball[3] = {
    0xE0,  // 111.....
    0xE0,  // 111.....
    0xE0   // 111.....
};

// 5x8 letter A
sprite letterA[5] = {
    0x70,  // .111....
    0x88,  // 1...1...
    0xF8,  // 11111...
    0x88,  // 1...1...
    0x88   // 1...1...
};
```

### ASCII Art Sprites
You can define sprites using ASCII art strings for easier visualization. Use `X`, `#`, `1`, or `*` for filled pixels:

```c
// Much easier to read!
sprite smiley[5] = {
    "..XXXX..",
    ".X....X.",
    "X.X..X.X",
    "X......X",
    ".X.XX.X."
};

sprite arrow[5] = {
    "...#....",
    "..###...",
    ".#####..",
    "...#....",
    "...#...."
};
```

Each string represents one row. Characters `X`, `#`, `1`, or `*` set a pixel; all other characters leave it off.

### Drawing Sprites
```c
draw(x, y, height, sprite_name);  // With explicit height
draw(x, y, sprite_name);          // Uses sprite's defined height
```

Examples:
```c
draw(10, 20, 3, ball);   // Draw ball at (10,20), 3 rows
draw(10, 20, ball);      // Draw ball at (10,20), all rows
```

## Built-in Functions and Statements

### Display
| Function/Statement | Description |
|-------------------|-------------|
| `clear` | Clear the display (statement) |
| `draw(x, y, h, sprite)` | Draw sprite at (x,y) with height h |
| `draw(x, y, sprite)` | Draw sprite using defined height |
| `drawnum(value, x, y)` | Draw 3-digit BCD number at (x,y) |

**Note**: `drawnum(value, x, y)` takes the value FIRST, then coordinates.

### Input
| Function | Description |
|----------|-------------|
| `key(n)` | Returns 1 if key n (0-F) is pressed |
| `waitkey()` | Blocks until key press, returns key number |

### Timing
| Function/Expression | Description |
|---------------------|-------------|
| `wait(ms)` | Wait approximately `ms` milliseconds (rounded down to 16ms timer ticks, minimum one tick) |
| `timer` | Read current delay timer value |

### Sound
| Function | Description |
|----------|-------------|
| `beep(duration)` | Play tone for duration ticks |

### Random
| Function | Description |
|----------|-------------|
| `rand(mask)` | Returns random number AND'd with mask |

Example: `rand(0xFF)` for 0-255, `rand(0x0F)` for 0-15.

### Collision Detection
| Expression | Description |
|------------|-------------|
| `collision` | Returns 1 if last draw had pixel collision |

Example:
```c
draw(x, y, ball);
if (collision) {
    // Ball hit something
}
```

## Input Keys

CHIP-8 uses a 16-key hexadecimal keypad (0-F). Common keyboard mappings:

| Key | Hex | Typical Use |
|-----|-----|-------------|
| 1-4 | 1-4 | Top row |
| Q,W,E,R | 4-7 | Second row |
| A,S,D,F | 7-A | Third row |
| Z,X,C,V | A-D | Bottom row |

**Common game controls:**
```c
if (key(5)) { y = y - 1; }  // W = up
if (key(8)) { y = y + 1; }  // S = down
if (key(7)) { x = x - 1; }  // A = left
if (key(9)) { x = x + 1; }  // D = right
if (key(0xF)) { fire(); }   // F = action
```

## Preprocessor

### #include
Include other SCL files to share code:
```c
#include "sprites.scl"
#include "utils.scl"

void main() {
    // Use sprites and functions from included files
}
```

Included files are processed before compilation, allowing you to organize
code across multiple files. Paths resolve relative to the including file;
circular includes are detected and skipped.

### #define
Simple text macros (no parameters):
```c
#define SPEED 2
#define DEBUG          // defined as 1 when no value given

void main() {
    byte x = SPEED;    // expands to: byte x = 2;
}
```

Note: with `#include`, reported error line numbers refer to the combined
preprocessed source, so they may be offset from the original file.

## Inline Assembly

Emit raw CHIP-8 opcodes for advanced use:
```c
asm { 0x00E0 }  // CLS - clear screen

asm {
    0x6000      // LD V0, 0
    0x6100      // LD V1, 0
}
```

## Comments

SCL supports both single-line and block comments:

```c
// This is a single-line comment

/* This is a
   block comment */

void main() {
    byte x = 10;  // Inline comment
}
```

## Complete Examples

### Moving Sprite
```c
sprite player[3] = { 0xE0, 0xE0, 0xE0 };

void main() {
    byte x = 32;
    byte y = 16;
    
    while (1) {
        clear;
        
        if (key(7)) { x = x - 1; }  // A = left
        if (key(9)) { x = x + 1; }  // D = right
        if (key(5)) { y = y - 1; }  // W = up
        if (key(8)) { y = y + 1; }  // S = down
        
        draw(x, y, player);
        wait(2);
    }
}
```

### Score Counter
```c
void main() {
    byte score = 0;
    
    while (1) {
        clear;
        drawnum(score, 28, 2);  // Draw score at (28, 2)
        
        if (key(0)) {
            score = score + 1;
            beep(2);
            while (key(0)) { }  // Wait for release
        }
        
        wait(2);
    }
}
```

### Using Functions
```c
sprite dot[1] = { 0x80 };

byte clamp(byte val, byte minv, byte maxv) {
    if (val < minv) { return minv; }
    if (val > maxv) { return maxv; }
    return val;
}

void draw_at(byte px, byte py) {
    draw(px, py, dot);
}

void main() {
    byte x = 32;
    byte y = 16;
    
    while (1) {
        if (key(7)) { x = x - 1; }
        if (key(9)) { x = x + 1; }
        if (key(5)) { y = y - 1; }
        if (key(8)) { y = y + 1; }
        
        x = clamp(x, 0, 63);
        y = clamp(y, 0, 31);
        
        clear;
        draw_at(x, y);
        wait(2);
    }
}
```

### Arrays for High Scores
```c
void main() {
    byte scores[5];
    byte i;
    byte highest = 0;
    
    // Initialize
    for (i = 0; i < 5; i = i + 1) {
        scores[i] = i * 10;
    }
    
    // Find highest
    for (i = 0; i < 5; i = i + 1) {
        if (scores[i] > highest) {
            highest = scores[i];
        }
    }
    
    drawnum(highest, 25, 14);
    while (1) { }
}
```

## Limitations

- **No floating point** - integers only (0-255)
- **No strings** - use sprites for text
- **~3KB program size** - code area is 0x200-0xDFF (3072 bytes)
- **~1.8KB** for arrays/globals - starting at 0x800, up to 0xF50
- **16-level call stack** - CHIP-8 hardware limit
- **~10 nested function calls** - runtime stack space

## Compiler Options

```bash
scl compile game.scl game.ch8 [options]
```

| Option | Description |
|--------|-------------|
| `--stats` | Print ROM usage, code/data split, peephole savings, and per-function register peaks |
| `--listing` | Write `game.ch8.lst`, an annotated disassembly interleaved with source lines |
| `-O0` | Disable the AST optimizer and peephole pass (for debugging codegen issues) |
| `--debug` | Verbose compilation trace |

Example `--stats` output:

```
--- Compilation stats ---
ROM size:     508 / 3072 bytes (16%)
  code:       502 bytes
  data:       6 bytes (sprites/tables)
peephole:     12 bytes removed
const folds:  19
register peaks per function (of 13 usable):
  main: 12
```

A function at peak 13 with spills listed is paying memory-access overhead
for the spilled values; if a hot loop is slow, reduce simultaneously-live
variables or move rarely-used state to globals.

## Compiler Warnings

The compiler provides helpful warnings to catch potential issues:

- **ROM size warnings** - Warns at 90% capacity, errors if exceeded
- **Memory pressure** - Errors when arrays and spill slots reach the runtime stack area
- **Memory usage** - Warns when arrays approach the runtime stack area
- **Unreachable code** - Warns about code after `return`, `break`, or `continue`

Warnings are displayed with source context:
```
game.scl:42:5: warning: Unreachable code after return/break/continue
    x = 10;
    ^
```

## Tips

1. **Use `wait()` in loops** - Controls speed and reduces flicker
2. **Check `collision` after `draw()`** - For hit detection
3. **Use `clear` before drawing** - Avoids XOR ghosting
4. **Clamp coordinates** - x: 0-63, y: 0-31 (or they wrap)
5. **Use constants** - Makes code readable and easy to tune
6. **Keep functions small** - Reduces register pressure
7. **Use globals sparingly** - Memory access is slower than registers

## Example Programs

See the `examples/` folder for complete, runnable games:

- **hello.scl** - Basic sprite drawing
- **animation.scl** - Frame-based animation
- **pong.scl** - Single-player Pong with paddle physics
- **snake.scl** - Snake game with food and scoring
- **breakout.scl** - Brick-breaking game with collision
- **tilemap.scl** - Level rendering from const ROM data tables

To run an example:
```bash
scl compile examples/snake.scl snake.ch8
scl run snake.ch8
```
