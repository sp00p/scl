# SCL Language Support for VS Code

Comprehensive language support for SCL (CHIP-8 scripting language).

## Features

- **Syntax highlighting** for `.scl` files
- **Bracket matching** and auto-closing
- **Comment toggling** (`//`)
- **Code folding**
- **Error diagnostics** - Shows compiler errors directly in the editor
- **Hover documentation** - Hover over keywords for documentation
- **Go to definition** - Jump to function and sprite definitions (F12)
- **Auto-completion** - Suggests keywords, functions, and symbols

## Diagnostics

The extension automatically compiles your SCL files on save and shows any errors
in the Problems panel and as red underlines in the editor.

To configure the compiler path:
1. Open Settings (Ctrl+,)
2. Search for "SCL"
3. Set `scl.compilerPath` to the path of your `scl2.0` executable

## Installation

### From Source

1. Navigate to the `scl-vscode` folder
2. Run:
   ```bash
   npm install
   npm run compile
   ```
3. Copy the folder to VS Code extensions:
   - Windows: `%USERPROFILE%\.vscode\extensions\scl-language`
   - macOS/Linux: `~/.vscode/extensions/scl-language`
4. Restart VS Code

### From VSIX

1. Build: `npm run compile && vsce package`
2. In VS Code, open Command Palette (Ctrl+Shift+P)
3. Run "Extensions: Install from VSIX..."
4. Select the generated `.vsix` file

## Highlighted Elements

- **Keywords**: `if`, `else`, `while`, `for`, `return`, `break`, `continue`
- **Declarations**: `const`, `global`, `asm`, `enum`
- **Types**: `void`, `byte`, `sprite`
- **Built-in functions**: `draw`, `clear`, `wait`, `key`, `waitkey`, `rand`, `beep`, `drawnum`
- **Built-in variables**: `collision`, `timer`
- **Numbers**: decimal and hexadecimal (`0xFF`)
- **Strings**: `"text"`
- **Comments**: `// line comments`

## Configuration

| Setting | Description | Default |
|---------|-------------|--------|
| `scl.compilerPath` | Path to the SCL compiler | `scl2.0` |

## Example

```scl
sprite ball[3] = { 0xE0, 0xE0, 0xE0 };

const SPEED = 2;

void main() {
    byte x = 32;
    byte y = 16;
    
    while (1) {
        clear;
        if (key(5)) y -= SPEED;  // W - up
        if (key(8)) y += SPEED;  // S - down
        draw(x, y, ball);
        wait(16);
    }
}
```
