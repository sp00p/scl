#include <gtest/gtest.h>
#include <chip8/compiler/lexer.h>
#include <chip8/compiler/parser.h>
#include <chip8/compiler/codegen.h>
#include <chip8/compiler/error_handler.h>

using namespace chip8::compiler;

class CodeGenTest : public ::testing::Test {
protected:
    ErrorHandler errorHandler;
    Lexer lexer;
    Parser parser{errorHandler};
    CodeGenerator codegen{errorHandler};
    
    std::vector<uint8_t> compile(const std::string& code) {
        auto tokens = lexer.tokenize(code);
        auto program = parser.parse(tokens);
        return codegen.generate(*program);
    }
    
    // Helper to extract opcode at byte offset
    uint16_t opcodeAt(const std::vector<uint8_t>& rom, size_t offset) {
        return (static_cast<uint16_t>(rom[offset]) << 8) | rom[offset + 1];
    }
};

// Basic program structure
TEST_F(CodeGenTest, GeneratesCallToMain) {
    auto rom = compile("void main() { }");
    
    ASSERT_GE(rom.size(), 4);
    // First instruction is LD VD, 0 (6D00) for runtime call stack
    EXPECT_EQ(opcodeAt(rom, 0), 0x6D00);
    // Second instruction should be CALL to main (2nnn)
    uint16_t opcode = opcodeAt(rom, 2);
    EXPECT_EQ(opcode & 0xF000, 0x2000);
}

TEST_F(CodeGenTest, GeneratesHaltLoop) {
    auto rom = compile("void main() { }");
    
    ASSERT_GE(rom.size(), 6);
    // Third instruction should be JP to itself (halt loop)
    uint16_t opcode = opcodeAt(rom, 4);
    EXPECT_EQ(opcode & 0xF000, 0x1000);
    // Jump target should be 0x204 (itself, after VD init and CALL)
    EXPECT_EQ(opcode & 0x0FFF, 0x204);
}

TEST_F(CodeGenTest, GeneratesReturnAtEndOfFunction) {
    auto rom = compile("void main() { }");
    
    // Find the RET instruction (00EE)
    bool foundRet = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (opcodeAt(rom, i) == 0x00EE) {
            foundRet = true;
            break;
        }
    }
    EXPECT_TRUE(foundRet);
}

// Clear screen
TEST_F(CodeGenTest, GeneratesClearOpcode) {
    auto rom = compile("void main() { clear; }");
    
    // Look for CLS (00E0)
    bool foundCls = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (opcodeAt(rom, i) == 0x00E0) {
            foundCls = true;
            break;
        }
    }
    EXPECT_TRUE(foundCls);
}

// Variable operations
TEST_F(CodeGenTest, GeneratesLoadImmediate) {
    auto rom = compile("void main() { byte x; x = 42; }");
    
    // Look for LD Vx, 42 (6xnn where nn = 0x2A)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF0FF) == 0x602A) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Arithmetic operations
TEST_F(CodeGenTest, GeneratesAddOpcode) {
    auto rom = compile("void main() { byte x; byte y; x = x + y; }");
    
    // Look for ADD Vx, Vy (8xy4)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF00F) == 0x8004) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CodeGenTest, GeneratesSubOpcode) {
    auto rom = compile("void main() { byte x; byte y; x = x - y; }");
    
    // Look for SUB Vx, Vy (8xy5)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF00F) == 0x8005) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Bitwise operations
TEST_F(CodeGenTest, GeneratesAndOpcode) {
    auto rom = compile("void main() { byte x; byte y; x = x & y; }");
    
    // Look for AND Vx, Vy (8xy2)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF00F) == 0x8002) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CodeGenTest, GeneratesOrOpcode) {
    auto rom = compile("void main() { byte x; byte y; x = x | y; }");
    
    // Look for OR Vx, Vy (8xy1)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF00F) == 0x8001) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CodeGenTest, GeneratesXorOpcode) {
    auto rom = compile("void main() { byte x; byte y; x = x ^ y; }");
    
    // Look for XOR Vx, Vy (8xy3)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF00F) == 0x8003) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Draw
TEST_F(CodeGenTest, GeneratesDrawOpcode) {
    auto rom = compile("void main() { byte x; byte y; draw(x, y, 5, 0); }");
    
    // Look for DRW Vx, Vy, 5 (Dxyn where n = 5)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF00F) == 0xD005) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Delay timer (wait)
TEST_F(CodeGenTest, GeneratesDelayTimerOpcodes) {
    auto rom = compile("void main() { wait(100); }");
    
    // Look for LD DT, Vx (Fx15)
    bool foundSetDt = false;
    // Look for LD Vx, DT (Fx07)
    bool foundGetDt = false;
    
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF0FF) == 0xF015) foundSetDt = true;
        if ((op & 0xF0FF) == 0xF007) foundGetDt = true;
    }
    
    EXPECT_TRUE(foundSetDt);
    EXPECT_TRUE(foundGetDt);
}

// Sound timer (beep)
TEST_F(CodeGenTest, GeneratesSoundTimerOpcode) {
    auto rom = compile("void main() { beep(30); }");
    
    // Look for LD ST, Vx (Fx18)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF0FF) == 0xF018) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Random
TEST_F(CodeGenTest, GeneratesRandomOpcode) {
    auto rom = compile("void main() { byte x; x = rand(255); }");
    
    // Look for RND Vx, 255 (CxFF)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF0FF) == 0xC0FF) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Key input
TEST_F(CodeGenTest, GeneratesSkipKeyOpcode) {
    auto rom = compile("void main() { byte x; if (key(5)) { x = 1; } }");
    
    // Look for SKP Vx (Ex9E)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF0FF) == 0xE09E) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CodeGenTest, GeneratesWaitKeyOpcode) {
    auto rom = compile("void main() { byte k; k = waitkey(); }");
    
    // Look for LD Vx, K (Fx0A)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF0FF) == 0xF00A) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Sprites
TEST_F(CodeGenTest, GeneratesSpriteData) {
    auto rom = compile("sprite ball[2] = { 0xC0, 0x30 }; void main() { }");
    
    // The sprite data (0xC0, 0x30) should be somewhere in the ROM
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i++) {
        if (rom[i] == 0xC0 && rom[i + 1] == 0x30) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CodeGenTest, GeneratesDrawWithCustomSprite) {
    auto rom = compile("sprite ball[2] = { 0xC0, 0xC0 }; void main() { byte x; byte y; draw(x, y, 2, ball); }");
    
    // Should have LD I, addr (Annn) pointing to sprite data
    bool foundLdI = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF000) == 0xA000) {
            foundLdI = true;
            break;
        }
    }
    EXPECT_TRUE(foundLdI);
}

// Control flow
TEST_F(CodeGenTest, GeneratesJumpOpcodes) {
    auto rom = compile("void main() { byte x; while (x < 10) { x = x + 1; } }");
    
    // Look for JP (1nnn)
    int jumpCount = 0;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF000) == 0x1000) {
            jumpCount++;
        }
    }
    // Should have at least 2 jumps: halt loop and loop back
    EXPECT_GE(jumpCount, 2);
}

TEST_F(CodeGenTest, GeneratesSkipOpcodes) {
    auto rom = compile("void main() { byte x; if (x == 5) { x = 0; } }");
    
    // Look for SE Vx, nn (3xnn) or SNE Vx, nn (4xnn)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        uint16_t op = opcodeAt(rom, i);
        if ((op & 0xF000) == 0x3000 || (op & 0xF000) == 0x4000) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Integration test
TEST_F(CodeGenTest, CompilesCompleteProgram) {
    auto rom = compile(R"(
        sprite ball[2] = { 0xC0, 0xC0 };
        
        void main() {
            byte x;
            byte y;
            
            x = 10;
            y = 5;
            
            while (1 == 1) {
                if (key(2)) {
                    y = y - 1;
                }
                if (key(8)) {
                    y = y + 1;
                }
                
                clear;
                draw(x, y, 2, ball);
                
                if (collision) {
                    beep(5);
                }
                
                wait(16);
            }
        }
    )");
    
    // Should generate a non-empty ROM
    EXPECT_GT(rom.size(), 20);
    
    // First instruction should be LD VD, 0 (6D00) for runtime call stack
    EXPECT_EQ(opcodeAt(rom, 0), 0x6D00);
    // Second instruction should be CALL main
    uint16_t secondOp = opcodeAt(rom, 2);
    EXPECT_EQ(secondOp & 0xF000, 0x2000);
}

// Compiler warning tests
class WarningTest : public ::testing::Test {
protected:
    ErrorHandler errorHandler{false};  // Don't throw on error
    Lexer lexer;
    Parser parser{errorHandler};
    CodeGenerator codegen{errorHandler};
    
    std::vector<uint8_t> compile(const std::string& code) {
        errorHandler.reset();
        auto tokens = lexer.tokenize(code);
        auto program = parser.parse(tokens);
        return codegen.generate(*program);
    }
};

TEST_F(WarningTest, WarnsOnUnreachableCode) {
    compile(R"(
        void main() {
            return;
            byte x;
            x = 10;
        }
    )");
    
    // Should have generated a warning about unreachable code
    EXPECT_TRUE(errorHandler.hasWarnings());
    bool found_unreachable_warning = false;
    for (const auto& w : errorHandler.getWarnings()) {
        if (w.message.find("Unreachable") != std::string::npos) {
            found_unreachable_warning = true;
            break;
        }
    }
    EXPECT_TRUE(found_unreachable_warning);
}

TEST_F(WarningTest, ErrorOnTooManyVariables) {
    // Try to use more than 13 variables (V1-VC, VE - V0, VD, VF reserved)
    compile(R"(
        void main() {
            byte a; byte b; byte c; byte d; byte e;
            byte f; byte g; byte h; byte i; byte j;
            byte k; byte l; byte m; byte n; byte o;
        }
    )");
    
    // Should have generated an error about running out of registers
    EXPECT_TRUE(errorHandler.hasErrors());
}

TEST_F(WarningTest, ErrorFormatsWithSourceLine) {
    // Trigger an error and check the formatted output
    compile(R"(
        void main() {
            undefined_var = 10;
        }
    )");
    
    // Should have an error
    EXPECT_TRUE(errorHandler.hasErrors());
    
    // Error message should contain formatted output
    const auto& err = errorHandler.getErrors()[0];
    std::string formatted = err.format();
    EXPECT_TRUE(formatted.find("error:") != std::string::npos);
}

// --- Strength reduction and new operators ---

// Multiply by a constant uses shift-add decomposition (SHL present, no loop)
TEST_F(CodeGenTest, MultiplyByConstUsesShifts) {
    auto rom = compile("void main() { byte a; byte b; a = 7; b = a * 10; }");
    ASSERT_FALSE(rom.empty());
    int shl_count = 0;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if ((opcodeAt(rom, i) & 0xF00F) == 0x800E) shl_count++;
    }
    EXPECT_GE(shl_count, 2); // x*10 needs at least 2 shifts
}

// Modulo by a power of two compiles to a single AND
TEST_F(CodeGenTest, ModuloPow2UsesAnd) {
    auto rom = compile("void main() { byte a; byte b; a = 23; b = a % 8; }");
    ASSERT_FALSE(rom.empty());
    bool found_and = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if ((opcodeAt(rom, i) & 0xF00F) == 0x8002) found_and = true;
    }
    EXPECT_TRUE(found_and);
}

// Constant shift counts unroll into shift instructions
TEST_F(CodeGenTest, ConstShiftUnrolls) {
    auto rom = compile("void main() { byte a; byte b; a = 3; b = a << 3; }");
    ASSERT_FALSE(rom.empty());
    int shl_count = 0;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if ((opcodeAt(rom, i) & 0xF00F) == 0x800E) shl_count++;
    }
    EXPECT_GE(shl_count, 3);
}

// --- Branch fusion ---

// if (a == const) uses the SE Vx, kk immediate skip - no boolean materialization
TEST_F(CodeGenTest, BranchFusionEqualConstUsesImmediateSkip) {
    auto rom = compile("void main() { byte a; a = 4; if (a == 5) { a = 1; } }");
    ASSERT_FALSE(rom.empty());
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (opcodeAt(rom, i) == 0x3105 || opcodeAt(rom, i) == 0x3205) found = true; // SE Vx, 5
    }
    EXPECT_TRUE(found);
}

// if (a == b) uses the SE Vx, Vy register skip
TEST_F(CodeGenTest, BranchFusionEqualRegUsesRegisterSkip) {
    auto rom = compile("void main() { byte a; byte b; a = 1; b = 2; if (a == b) { a = 3; } }");
    ASSERT_FALSE(rom.empty());
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if ((opcodeAt(rom, i) & 0xF00F) == 0x5000) found = true; // SE Vx, Vy
    }
    EXPECT_TRUE(found);
}

// while(1) emits no condition check at all
TEST_F(CodeGenTest, WhileOneHasNoConditionCheck) {
    auto rom_inf = compile("void main() { byte a; a = 0; while (1) { a = a + 1; } }");
    auto rom_cond = compile("void main() { byte a; a = 0; while (a < 5) { a = a + 1; } }");
    ASSERT_FALSE(rom_inf.empty());
    ASSERT_FALSE(rom_cond.empty());
    EXPECT_LT(rom_inf.size(), rom_cond.size()); // no test emitted for while(1)
}

// --- Const ROM tables ---

// Table bytes are emitted into the ROM and reads use LD I / ADD I / LD V0,[I]
TEST_F(CodeGenTest, ConstTableEmitsDataAndIndexedRead) {
    auto rom = compile("const byte tbl[] = { 0xAA, 0xBB, 0xCC }; "
                       "void main() { byte i; byte v; i = 1; v = tbl[i]; }");
    ASSERT_FALSE(rom.empty());
    // Data bytes present consecutively
    bool data_found = false;
    for (size_t i = 0; i + 2 < rom.size(); i++) {
        if (rom[i] == 0xAA && rom[i+1] == 0xBB && rom[i+2] == 0xCC) data_found = true;
    }
    EXPECT_TRUE(data_found);
    // Indexed read sequence: ADD I, V0 (F01E) then LD V0, [I] (F065)
    bool read_found = false;
    for (size_t i = 0; i + 3 < rom.size(); i += 2) {
        if (opcodeAt(rom, i) == 0xF01E && opcodeAt(rom, i + 2) == 0xF065) read_found = true;
    }
    EXPECT_TRUE(read_found);
}

// Writing to a const table is a compile error
TEST_F(CodeGenTest, ConstTableWriteIsError) {
    bool rejected = false;
    try {
        compile("const byte tbl[] = { 1, 2 }; void main() { tbl[0] = 9; }");
        rejected = errorHandler.hasErrors();
    } catch (const std::exception&) {
        rejected = true; // error handler configured to throw
    }
    EXPECT_TRUE(rejected);
}

// Sprite/table data must survive the peephole pass intact
TEST_F(CodeGenTest, PeepholeDoesNotRewriteData) {
    // 0x70 0x00 looks like ADD V0,0 (a peephole no-op pattern); it must
    // survive inside sprite data
    auto rom = compile("sprite s[2] = { 0x70, 0x00 }; "
                       "void main() { byte x; x = 1; x = 2; draw(0, 0, 2, s); }");
    ASSERT_FALSE(rom.empty());
    bool data_found = false;
    for (size_t i = 0; i + 1 < rom.size(); i++) {
        if (rom[i] == 0x70 && rom[i+1] == 0x00) data_found = true;
    }
    EXPECT_TRUE(data_found);
}

// --- Peephole patterns 5/6: dead branches around empty bodies ---

// An if with an empty body compiles away entirely (skip + jump-to-next pair)
TEST_F(CodeGenTest, EmptyIfBodyIsFree) {
    auto with_if = compile("void main() { byte a; a = 1; if (a == 1) { } }");
    auto without  = compile("void main() { byte a; a = 1; }");
    ASSERT_FALSE(with_if.empty());
    EXPECT_EQ(with_if.size(), without.size());
}

// --- Return-value-in-VE convention ---

// A value-returning call needs no memory round-trip when VE is free:
// the return lands in VE and is copied to the destination register
TEST_F(CodeGenTest, ReturnUsesVERegister) {
    auto rom = compile("byte five() { return 5; } "
                       "void main() { byte x; x = five(); }");
    ASSERT_FALSE(rom.empty());
    // Callee loads the return value into VE: LD VE, 5 (6E05)
    bool found = false;
    for (size_t i = 0; i + 1 < rom.size(); i += 2) {
        if (opcodeAt(rom, i) == 0x6E05) found = true;
    }
    EXPECT_TRUE(found);
}
