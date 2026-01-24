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

// Test: Multiple variables compile
TEST(IntegrationTest, MultipleVariables) {
    const char* source = "void main() { byte a; byte b; byte c; byte d; a = 1; b = 2; c = 3; d = 4; }";
    auto bytecode = compileSource(source);
    ASSERT_FALSE(bytecode.empty());
    // Should have multiple LD Vx, nn instructions
    int ldCount = 0;
    for (size_t i = 0; i + 1 < bytecode.size(); i += 2) {
        if ((bytecode[i] & 0xF0) == 0x60) {
            ldCount++;
        }
    }
    EXPECT_GE(ldCount, 4); // At least 4 loads for a=1, b=2, c=3, d=4
}
