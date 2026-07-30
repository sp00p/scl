#include <gtest/gtest.h>
#include <chip8/compiler/lexer.h>
#include <chip8/compiler/parser.h>
#include <chip8/compiler/codegen.h>
#include <chip8/compiler/preprocessor.h>
#include <chip8/compiler/optimizer.h>
#include <chip8/compiler/error_handler.h>
#include <chip8/emulator.h>
#include <cstring>

using namespace chip8::compiler;

// Headless emulator for testing - runs without display
class HeadlessEmulator : public Chip8 {
public:
    // Run for a specific number of cycles
    void runCycles(int cycles) {
        for (int i = 0; i < cycles && !halted; i++) {
            emulateCycle();
        }
    }
    
    // Run until PC doesn't change (halted) or max cycles
    void runUntilHalt(int max_cycles = 10000) {
        uint16_t last_pc = 0;
        int same_count = 0;
        for (int i = 0; i < max_cycles; i++) {
            last_pc = getPC();
            emulateCycle();
            if (getPC() == last_pc) {
                same_count++;
                if (same_count > 10) break; // Likely in infinite loop or halted
            } else {
                same_count = 0;
            }
        }
    }
    
    bool halted = false;
};

// Helper to compile SCL source to bytecode
std::vector<uint8_t> compileSource(const std::string& source) {
    Preprocessor preproc;
    std::string processed = preproc.process(source);
    
    Lexer lexer;
    auto tokens = lexer.tokenize(processed);
    
    ErrorHandler errorHandler(true);
    Parser parser(errorHandler);
    auto ast = parser.parse(tokens);
    
    // Optimize
    Optimizer optimizer;
    optimizer.optimize(*ast);
    
    CodeGenerator codegen(errorHandler);
    return codegen.generate(*ast);
}

// Load bytecode into emulator
void loadBytecode(Chip8& emu, const std::vector<uint8_t>& bytecode) {
    emu.reset();
    uint8_t* mem = emu.getMemory();
    std::memcpy(mem + 0x200, bytecode.data(), bytecode.size());
}

// Test: Simple register assignment compiles
TEST(IntegrationTest, RegisterAssignment) {
    const char* source = "void main() { byte x; x = 42; }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Look for LD Vx, 42 (6xnn where nn = 0x2A)
    bool found = false;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x60 && bytecode[i+1] == 0x2A) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: Arithmetic operations compile
TEST(IntegrationTest, ArithmeticOps) {
    const char* source = "void main() { byte a; byte b; a = 10; b = 5; a = a + b; }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Look for ADD (8xy4)
    bool found = false;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x80 && (bytecode[i+1] & 0x0F) == 0x04) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: If statement compiles
TEST(IntegrationTest, IfStatement) {
    const char* source = "void main() { byte x; byte result; x = 5; result = 0; if (x == 5) { result = 1; } }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    EXPECT_GE(bytecode.size(), 10); // Has conditional code
}

// Test: While loop compiles
TEST(IntegrationTest, WhileLoop) {
    const char* source = "void main() { byte counter; byte limit; counter = 0; limit = 5; while (counter != limit) { counter = counter + 1; } }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Look for JP (1nnn) - loops have jumps
    bool foundJp = false;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x10) {
            foundJp = true;
            break;
        }
    }
    EXPECT_TRUE(foundJp);
}

// Test: Function call compiles
TEST(IntegrationTest, FunctionCall) {
    const char* source = "void doubleIt(byte x) { x = x + x; } void main() { byte val; val = 7; doubleIt(val); }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Look for CALL (2nnn)
    int callCount = 0;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x20) {
            callCount++;
        }
    }
    EXPECT_GE(callCount, 2); // CALL main and CALL doubleIt
}

// Test: Constant expression compiles
TEST(IntegrationTest, ConstantFolding) {
    const char* source = "void main() { byte x; x = 2 + 3; }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Should have LD Vx, nn instruction
    bool found = false;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x60) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: Bitwise operations compile
TEST(IntegrationTest, BitwiseOps) {
    const char* source = "void main() { byte a; byte b; byte c; a = 0xF0; b = 0x0F; c = a | b; }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Look for OR (8xy1)
    bool found = false;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x80 && (bytecode[i+1] & 0x0F) == 0x01) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: Preprocessor include (simulated)
TEST(IntegrationTest, PreprocessorBasic) {
    Preprocessor preproc;
    
    // Test basic processing without includes
    std::string source = "void main() { byte x; x = 1; }";
    std::string result = preproc.process(source);
    
    EXPECT_FALSE(result.empty());
    EXPECT_TRUE(result.find("void main") != std::string::npos);
    EXPECT_FALSE(preproc.hasErrors());
}

// Test: Optimizer constant folding
TEST(IntegrationTest, OptimizerConstFold) {
    const char* source = "void main() { byte x; x = 10 + 20; }";
    
    Lexer lexer;
    auto tokens = lexer.tokenize(source);
    
    ErrorHandler errorHandler(true);
    Parser parser(errorHandler);
    auto ast = parser.parse(tokens);
    
    Optimizer optimizer;
    optimizer.optimize(*ast);
    
    // Check that constants were folded
    EXPECT_GT(optimizer.getConstantsFolded(), 0);
}

// Test: Clear screen instruction
TEST(IntegrationTest, ClearScreen) {
    const char* source = "void main() { clear; }";
    
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    
    // Look for CLS (00E0) in bytecode
    bool found = false;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if (bytecode[i] == 0x00 && bytecode[i+1] == 0xE0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

// Test: Multiple variables compile. Values stored to variables that are
// never read may be eliminated as dead stores, so only require that the
// program compiles and contains at least one load.
TEST(IntegrationTest, MultipleVariables) {
    const char* source = "void main() { byte a; byte b; byte c; byte d; a = 1; b = 2; c = 3; d = 4; }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    int ldCount = 0;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x60) {
            ldCount++;
        }
    }
    EXPECT_GE(ldCount, 1);
}

// ============================================================
// Behavioral tests: compile, execute on the emulator core, and
// assert on results written to globals (allocated from 0x800 in
// declaration order).
// ============================================================

static uint8_t globalAt(Chip8& emu, int index) {
    return emu.getMemory()[0x800 + index];
}

static void compileAndRun(HeadlessEmulator& emu, const std::string& source, int max_cycles = 30000) {
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    loadBytecode(emu, bytecode);
    emu.runUntilHalt(max_cycles);
}

// Arithmetic through runtime register paths (variables defeat const folding)
TEST(BehaviorTest, ArithmeticOperators) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2; global byte g3;"
        "global byte g4; global byte g5; global byte g6;"
        "void main() {"
        "  byte a; byte b;"
        "  a = 23; b = 7;"
        "  g0 = a % b;"      // 23 % 7 = 2   (runtime loop)
        "  g1 = a % 8;"      // 23 % 8 = 7   (AND mask)
        "  g2 = a * 10;"     // 230          (shift-add)
        "  a = 45;"
        "  g3 = a / b;"      // 45 / 7 = 6   (runtime loop)
        "  g4 = a / 8;"      // 45 / 8 = 5   (shifts)
        "  a = 3;"
        "  g5 = a << 4;"     // 48           (unrolled)
        "  b = 2;"
        "  g6 = a << b;"     // 12           (runtime loop)
        "}");
    EXPECT_EQ(globalAt(emu, 0), 2);
    EXPECT_EQ(globalAt(emu, 1), 7);
    EXPECT_EQ(globalAt(emu, 2), 230);
    EXPECT_EQ(globalAt(emu, 3), 6);
    EXPECT_EQ(globalAt(emu, 4), 5);
    EXPECT_EQ(globalAt(emu, 5), 48);
    EXPECT_EQ(globalAt(emu, 6), 12);
}

TEST(BehaviorTest, UnaryMinusAndShiftRight) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2;"
        "void main() {"
        "  byte a;"
        "  a = 23;"
        "  g0 = -a;"         // 256 - 23 = 233
        "  g1 = -5;"         // literal fold: 251
        "  a = 200;"
        "  g2 = a >> 3;"     // 25
        "}");
    EXPECT_EQ(globalAt(emu, 0), 233);
    EXPECT_EQ(globalAt(emu, 1), 251);
    EXPECT_EQ(globalAt(emu, 2), 25);
}

TEST(BehaviorTest, OperatorPrecedence) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2;"
        "void main() {"
        "  byte a; byte b; byte c;"
        "  a = 2; b = 3; c = 4;"
        "  g0 = a + b * c;"      // 14, not 20
        "  g1 = (a + b) * c;"    // 20
        "  g2 = a << 1 + 2;"     // 2 << 3 = 16, not 8+2
        "}");
    EXPECT_EQ(globalAt(emu, 0), 14);
    EXPECT_EQ(globalAt(emu, 1), 20);
    EXPECT_EQ(globalAt(emu, 2), 16);
}

// Every comparison operator, both outcomes, through the fused-branch path
TEST(BehaviorTest, ComparisonBranches) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2; global byte g3;"
        "global byte g4; global byte g5; global byte g6; global byte g7;"
        "void main() {"
        "  byte a; byte b;"
        "  a = 3; b = 5;"
        "  if (a < b)  { g0 = 1; } else { g0 = 2; }"   // 1
        "  if (a > b)  { g1 = 1; } else { g1 = 2; }"   // 2
        "  if (a <= 3) { g2 = 1; } else { g2 = 2; }"   // 1
        "  if (a >= 4) { g3 = 1; } else { g3 = 2; }"   // 2
        "  if (a == 3) { g4 = 1; } else { g4 = 2; }"   // 1
        "  if (a != 3) { g5 = 1; } else { g5 = 2; }"   // 2
        "  if (a == b) { g6 = 1; } else { g6 = 2; }"   // 2
        "  if (a != b) { g7 = 1; } else { g7 = 2; }"   // 1
        "}");
    EXPECT_EQ(globalAt(emu, 0), 1);
    EXPECT_EQ(globalAt(emu, 1), 2);
    EXPECT_EQ(globalAt(emu, 2), 1);
    EXPECT_EQ(globalAt(emu, 3), 2);
    EXPECT_EQ(globalAt(emu, 4), 1);
    EXPECT_EQ(globalAt(emu, 5), 2);
    EXPECT_EQ(globalAt(emu, 6), 2);
    EXPECT_EQ(globalAt(emu, 7), 1);
}

// Short-circuit && / || and ! through the fused-branch path
TEST(BehaviorTest, LogicalBranches) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2; global byte g3; global byte g4;"
        "void main() {"
        "  byte a; byte b;"
        "  a = 3; b = 5;"
        "  if (a < b && b < 10) { g0 = 1; } else { g0 = 2; }"  // 1
        "  if (a < b && b > 10) { g1 = 1; } else { g1 = 2; }"  // 2
        "  if (a > b || b == 5) { g2 = 1; } else { g2 = 2; }"  // 1
        "  if (a > b || b != 5) { g3 = 1; } else { g3 = 2; }"  // 2
        "  if (!(a == b))       { g4 = 1; } else { g4 = 2; }"  // 1
        "}");
    EXPECT_EQ(globalAt(emu, 0), 1);
    EXPECT_EQ(globalAt(emu, 1), 2);
    EXPECT_EQ(globalAt(emu, 2), 1);
    EXPECT_EQ(globalAt(emu, 3), 2);
    EXPECT_EQ(globalAt(emu, 4), 1);
}

// Loops with fused conditions produce correct iteration counts
TEST(BehaviorTest, LoopSemantics) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2;"
        "void main() {"
        "  byte i; byte sum;"
        "  sum = 0;"
        "  for (i = 0; i < 10; i++) { sum = sum + i; }"
        "  g0 = sum;"                                   // 45
        "  sum = 0; i = 0;"
        "  while (i < 5) { sum = sum + 2; i++; }"
        "  g1 = sum;"                                   // 10
        "  sum = 0;"
        "  for (i = 0; i < 10; i++) {"
        "    if (i == 3) { continue; }"
        "    if (i == 7) { break; }"
        "    sum = sum + 1;"
        "  }"
        "  g2 = sum;"                                   // iterations 0,1,2,4,5,6 = 6
        "}");
    EXPECT_EQ(globalAt(emu, 0), 45);
    EXPECT_EQ(globalAt(emu, 1), 10);
    EXPECT_EQ(globalAt(emu, 2), 6);
}

// Const ROM tables: indexed reads with constant and computed indices
TEST(BehaviorTest, ConstTableReads) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "const byte tbl[] = { 5, 9, 13, 21 };"
        "global byte g0; global byte g1; global byte g2;"
        "void main() {"
        "  byte i;"
        "  i = 2;"
        "  g0 = tbl[0];"       // 5
        "  g1 = tbl[i];"       // 13
        "  g2 = tbl[i + 1];"   // 21
        "}");
    EXPECT_EQ(globalAt(emu, 0), 5);
    EXPECT_EQ(globalAt(emu, 1), 13);
    EXPECT_EQ(globalAt(emu, 2), 21);
}

// Conditions still work as values (non-branch context)
TEST(BehaviorTest, ComparisonAsValue) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1;"
        "void main() {"
        "  byte a; byte b;"
        "  a = 3; b = 5;"
        "  g0 = (a < b);"      // 1
        "  g1 = (a == b);"     // 0
        "}");
    EXPECT_EQ(globalAt(emu, 0), 1);
    EXPECT_EQ(globalAt(emu, 1), 0);
}

// Function calls still preserve caller registers with the new codegen
TEST(BehaviorTest, FunctionCallsPreserveState) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1;"
        "byte add(byte x, byte y) { return x + y; }"
        "void main() {"
        "  byte a; byte b; byte c;"
        "  a = 10; b = 20;"
        "  c = add(a, b);"
        "  g0 = c;"            // 30
        "  g1 = a + b;"        // 30 - a and b survived the call
        "}");
    EXPECT_EQ(globalAt(emu, 0), 30);
    EXPECT_EQ(globalAt(emu, 1), 30);
}

// Return-in-VE: fast path (VE free in caller) and slow path (VE live)
TEST(BehaviorTest, ReturnValueUnderRegisterPressure) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2;"
        "byte add(byte x, byte y) { return x + y; }"
        "void main() {"
        // 13 locals: V1..VC plus VE - forces the VE-live slow path
        "  byte a; byte b; byte c; byte d; byte e; byte f; byte g;"
        "  byte h; byte i; byte j; byte k; byte l; byte m;"
        "  a=1; b=2; c=3; d=4; e=5; f=6; g=7; h=8; i=9; j=10; k=11; l=12; m=13;"
        "  g0 = add(20, 30);"   // 50 - return survives the full-restore path
        "  g1 = m;"             // 13 - the VE-resident local survived the call
        "  g2 = a;"             // 1  - low registers restored
        "}");
    EXPECT_EQ(globalAt(emu, 0), 50);
    EXPECT_EQ(globalAt(emu, 1), 13);
    EXPECT_EQ(globalAt(emu, 2), 1);
}

// Transitive clobber: mid() itself uses few registers but calls deep(),
// which uses many. The caller of mid() must save everything deep() touches.
TEST(BehaviorTest, TransitiveRegisterClobber) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1;"
        "byte deep(byte x) {"
        "  byte a; byte b; byte c; byte d; byte e; byte f; byte g;"
        "  a=1; b=2; c=3; d=4; e=5; f=6; g=7;"
        "  return x + a + b + c + d + e + f + g;"               // x + 28
        "}"
        "byte mid(byte x) { return deep(x); }"
        "void main() {"
        "  byte p; byte q; byte r; byte s; byte t; byte u;"
        "  p=10; q=20; r=30; s=40; t=50; u=60;"
        "  g0 = mid(2);"                                        // 30
        "  g1 = p + q + r + s + t + u;"                         // 210 - all intact
        "}");
    EXPECT_EQ(globalAt(emu, 0), 30);
    EXPECT_EQ(globalAt(emu, 1), 210);
}

// Nested/recursive-style call chains still return correctly
TEST(BehaviorTest, ChainedCallReturns) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0;"
        "byte inc(byte x) { return x + 1; }"
        "byte twice_inc(byte x) { return inc(inc(x)); }"
        "void main() { g0 = twice_inc(5); }");                  // 7
    EXPECT_EQ(globalAt(emu, 0), 7);
}

// The register allocator's headline feature: more live variables than
// physical registers, all read back correctly (spilled values included).
TEST(BehaviorTest, SpilledVariablesComputeCorrectly) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1; global byte g2;"
        "void main() {"
        "  byte v1; byte v2; byte v3; byte v4; byte v5;"
        "  byte v6; byte v7; byte v8; byte v9; byte v10;"
        "  byte v11; byte v12; byte v13; byte v14; byte v15;"
        "  byte v16; byte v17; byte v18; byte v19; byte v20;"
        "  v1=1; v2=2; v3=3; v4=4; v5=5; v6=6; v7=7; v8=8; v9=9; v10=10;"
        "  v11=11; v12=12; v13=13; v14=14; v15=15; v16=16; v17=17; v18=18; v19=19; v20=20;"
        // every variable is READ here, so none are dead - all 20 are live at once
        "  g0 = v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10;"      // 55
        "  g1 = v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19 + v20;" // 155
        "  g2 = v1 + v20;"                                              // 21
        "}");
    EXPECT_EQ(globalAt(emu, 0), 55);
    EXPECT_EQ(globalAt(emu, 1), 155);
    EXPECT_EQ(globalAt(emu, 2), 21);
}

// Spilled values must survive loops and function calls
TEST(BehaviorTest, SpillsSurviveLoopsAndCalls) {
    HeadlessEmulator emu;
    compileAndRun(emu,
        "global byte g0; global byte g1;"
        "byte double_it(byte x) { return x + x; }"
        "void main() {"
        "  byte a1; byte a2; byte a3; byte a4; byte a5; byte a6; byte a7;"
        "  byte a8; byte a9; byte a10; byte a11; byte a12; byte a13; byte a14;"
        "  byte i; byte sum;"
        "  a1=1; a2=2; a3=3; a4=4; a5=5; a6=6; a7=7;"
        "  a8=8; a9=9; a10=10; a11=11; a12=12; a13=13; a14=14;"
        "  sum = 0;"
        "  for (i = 0; i < 3; i++) {"
        "    sum = sum + double_it(a14);"      // 3 * 28 = 84
        "  }"
        "  g0 = sum;"
        "  g1 = a1 + a2 + a13 + a14;"          // 30
        "}");
    EXPECT_EQ(globalAt(emu, 0), 84);
    EXPECT_EQ(globalAt(emu, 1), 30);
}
