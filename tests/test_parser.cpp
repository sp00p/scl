#include <gtest/gtest.h>
#include <chip8/compiler/lexer.h>
#include <chip8/compiler/parser.h>
#include <chip8/compiler/error_handler.h>

using namespace chip8::compiler;

class ParserTest : public ::testing::Test {
protected:
    ErrorHandler errorHandler;
    Lexer lexer;
    Parser parser{errorHandler};
    
    std::unique_ptr<ProgramNode> parse(const std::string& code) {
        auto tokens = lexer.tokenize(code);
        return parser.parse(tokens);
    }
};

// Function parsing
TEST_F(ParserTest, ParsesEmptyFunction) {
    auto program = parse("void main() { }");
    
    ASSERT_EQ(program->functions.size(), 1);
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "main");
}

TEST_F(ParserTest, ParsesMultipleFunctions) {
    auto program = parse("void foo() { } void bar() { }");
    
    ASSERT_EQ(program->functions.size(), 2);
    auto* func1 = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* func2 = dynamic_cast<FunctionNode*>(program->functions[1].get());
    EXPECT_EQ(func1->name, "foo");
    EXPECT_EQ(func2->name, "bar");
}

// Variable declarations
TEST_F(ParserTest, ParsesVariableDeclaration) {
    auto program = parse("void main() { byte x; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    ASSERT_EQ(block->statements.size(), 1);
    
    auto* var_decl = dynamic_cast<VariableDeclNode*>(block->statements[0].get());
    ASSERT_NE(var_decl, nullptr);
    EXPECT_EQ(var_decl->name, "x");
}

// Assignments
TEST_F(ParserTest, ParsesSimpleAssignment) {
    auto program = parse("void main() { byte x; x = 5; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    ASSERT_EQ(block->statements.size(), 2);
    
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->name, "x");
    
    auto* value = dynamic_cast<NumberExprNode*>(assignment->value.get());
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->value, 5);
}

TEST_F(ParserTest, ParsesBinaryExpressionAssignment) {
    auto program = parse("void main() { byte x; x = 3 + 2; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    
    auto* bin_expr = dynamic_cast<BinaryExprNode*>(assignment->value.get());
    ASSERT_NE(bin_expr, nullptr);
    EXPECT_EQ(bin_expr->op, TokenType::PLUS);
}

// Control flow
TEST_F(ParserTest, ParsesIfStatement) {
    auto program = parse("void main() { byte x; if (x == 5) { x = 0; } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    ASSERT_EQ(block->statements.size(), 2);
    
    auto* if_node = dynamic_cast<IfNode*>(block->statements[1].get());
    ASSERT_NE(if_node, nullptr);
    EXPECT_NE(if_node->condition, nullptr);
    EXPECT_NE(if_node->then_branch, nullptr);
    EXPECT_EQ(if_node->else_branch, nullptr);
}

TEST_F(ParserTest, ParsesIfElseStatement) {
    auto program = parse("void main() { byte x; if (x == 5) { x = 0; } else { x = 1; } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* if_node = dynamic_cast<IfNode*>(block->statements[1].get());
    
    ASSERT_NE(if_node, nullptr);
    EXPECT_NE(if_node->else_branch, nullptr);
}

TEST_F(ParserTest, ParsesWhileLoop) {
    auto program = parse("void main() { byte x; while (x < 10) { x = x + 1; } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    ASSERT_EQ(block->statements.size(), 2);
    
    auto* while_node = dynamic_cast<WhileNode*>(block->statements[1].get());
    ASSERT_NE(while_node, nullptr);
    EXPECT_NE(while_node->condition, nullptr);
    EXPECT_NE(while_node->body, nullptr);
}

TEST_F(ParserTest, ParsesForLoop) {
    auto program = parse("void main() { byte i; for (i = 0; i < 10; i = i + 1) { clear(); } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    ASSERT_EQ(block->statements.size(), 2);
    
    auto* for_node = dynamic_cast<ForNode*>(block->statements[1].get());
    ASSERT_NE(for_node, nullptr);
    EXPECT_NE(for_node->init, nullptr);
    EXPECT_NE(for_node->condition, nullptr);
    EXPECT_NE(for_node->increment, nullptr);
    EXPECT_NE(for_node->body, nullptr);
}

// Built-in statements
TEST_F(ParserTest, ParsesClear) {
    auto program = parse("void main() { clear; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    
    auto* clear = dynamic_cast<ClearNode*>(block->statements[0].get());
    ASSERT_NE(clear, nullptr);
}

TEST_F(ParserTest, ParsesClearWithParens) {
    auto program = parse("void main() { clear(); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    
    auto* clear = dynamic_cast<ClearNode*>(block->statements[0].get());
    ASSERT_NE(clear, nullptr);
}

TEST_F(ParserTest, ParsesWait) {
    auto program = parse("void main() { wait(100); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    
    auto* wait = dynamic_cast<WaitNode*>(block->statements[0].get());
    ASSERT_NE(wait, nullptr);
    EXPECT_EQ(wait->type, WaitNode::WaitType::FIXED);
    EXPECT_EQ(wait->fixed_duration, 100);
}

TEST_F(ParserTest, ParsesDraw) {
    auto program = parse("void main() { byte x; byte y; draw(x, y, 5, 0); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    
    auto* draw = dynamic_cast<DrawNode*>(block->statements[2].get());
    ASSERT_NE(draw, nullptr);
    // x_expr and y_expr are now expression pointers
    auto* x_var = dynamic_cast<VariableExprNode*>(draw->x_expr.get());
    auto* y_var = dynamic_cast<VariableExprNode*>(draw->y_expr.get());
    ASSERT_NE(x_var, nullptr);
    ASSERT_NE(y_var, nullptr);
    EXPECT_EQ(x_var->name, "x");
    EXPECT_EQ(y_var->name, "y");
    EXPECT_EQ(draw->height, 5);
    EXPECT_EQ(draw->sprite_id, 0);
}

// New features
TEST_F(ParserTest, ParsesBeep) {
    auto program = parse("void main() { beep(30); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    
    auto* beep = dynamic_cast<BeepNode*>(block->statements[0].get());
    ASSERT_NE(beep, nullptr);
}

TEST_F(ParserTest, ParsesSpriteDefinition) {
    auto program = parse("sprite ball[2] = { 0xC0, 0xC0 }; void main() { }");
    
    ASSERT_GE(program->functions.size(), 2);
    auto* sprite = dynamic_cast<SpriteDefNode*>(program->functions[0].get());
    ASSERT_NE(sprite, nullptr);
    EXPECT_EQ(sprite->name, "ball");
    EXPECT_EQ(sprite->height, 2);
    ASSERT_EQ(sprite->data.size(), 2);
    EXPECT_EQ(sprite->data[0], 0xC0);
    EXPECT_EQ(sprite->data[1], 0xC0);
}

TEST_F(ParserTest, ParsesKeyExpression) {
    auto program = parse("void main() { byte x; if (key(5)) { x = 1; } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* if_node = dynamic_cast<IfNode*>(block->statements[1].get());

    // A bare expression condition is used directly (truthiness is tested at
    // codegen time), so the condition should be the KeyExprNode itself
    auto* key_expr = dynamic_cast<KeyExprNode*>(if_node->condition.get());
    ASSERT_NE(key_expr, nullptr);
}

TEST_F(ParserTest, ParsesRandExpression) {
    auto program = parse("void main() { byte x; x = rand(255); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    
    auto* rand_expr = dynamic_cast<RandExprNode*>(assignment->value.get());
    ASSERT_NE(rand_expr, nullptr);
}

TEST_F(ParserTest, ParsesCollisionExpression) {
    auto program = parse("void main() { byte c; c = collision; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    
    auto* coll_expr = dynamic_cast<CollisionExprNode*>(assignment->value.get());
    ASSERT_NE(coll_expr, nullptr);
}

TEST_F(ParserTest, ParsesWaitKeyExpression) {
    auto program = parse("void main() { byte k; k = waitkey(); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    
    auto* waitkey_expr = dynamic_cast<WaitKeyExprNode*>(assignment->value.get());
    ASSERT_NE(waitkey_expr, nullptr);
}

TEST_F(ParserTest, ParsesDrawWithSpriteName) {
    auto program = parse("sprite ball[2] = { 0xC0, 0xC0 }; void main() { byte x; byte y; draw(x, y, 2, ball); }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[1].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* draw = dynamic_cast<DrawNode*>(block->statements[2].get());
    
    ASSERT_NE(draw, nullptr);
    EXPECT_EQ(draw->sprite_name, "ball");
    EXPECT_EQ(draw->sprite_id, -1);
}

// Comparison operators
TEST_F(ParserTest, ParsesLessEqualCondition) {
    auto program = parse("void main() { byte x; if (x <= 10) { } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* if_node = dynamic_cast<IfNode*>(block->statements[1].get());
    auto* condition = dynamic_cast<ConditionNode*>(if_node->condition.get());
    
    EXPECT_EQ(condition->op, TokenType::LESS_EQUAL);
}

TEST_F(ParserTest, ParsesGreaterEqualCondition) {
    auto program = parse("void main() { byte x; if (x >= 10) { } }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* if_node = dynamic_cast<IfNode*>(block->statements[1].get());
    auto* condition = dynamic_cast<ConditionNode*>(if_node->condition.get());
    
    EXPECT_EQ(condition->op, TokenType::GREATER_EQUAL);
}

// Bitwise operators
TEST_F(ParserTest, ParsesBitwiseAnd) {
    auto program = parse("void main() { byte x; x = 5 & 3; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    auto* bin_expr = dynamic_cast<BinaryExprNode*>(assignment->value.get());
    
    ASSERT_NE(bin_expr, nullptr);
    EXPECT_EQ(bin_expr->op, TokenType::AMPERSAND);
}

TEST_F(ParserTest, ParsesBitwiseOr) {
    auto program = parse("void main() { byte x; x = 5 | 3; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    auto* bin_expr = dynamic_cast<BinaryExprNode*>(assignment->value.get());
    
    ASSERT_NE(bin_expr, nullptr);
    EXPECT_EQ(bin_expr->op, TokenType::PIPE);
}

TEST_F(ParserTest, ParsesBitwiseXor) {
    auto program = parse("void main() { byte x; x = 5 ^ 3; }");
    
    auto* func = dynamic_cast<FunctionNode*>(program->functions[0].get());
    auto* block = dynamic_cast<BlockNode*>(func->body.get());
    auto* assignment = dynamic_cast<AssignmentNode*>(block->statements[1].get());
    auto* bin_expr = dynamic_cast<BinaryExprNode*>(assignment->value.get());
    
    ASSERT_NE(bin_expr, nullptr);
    EXPECT_EQ(bin_expr->op, TokenType::CARET);
}
