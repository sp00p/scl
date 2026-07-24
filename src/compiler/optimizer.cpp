#include <chip8/compiler/optimizer.h>

namespace chip8 {
namespace compiler {

Optimizer::Optimizer() = default;

int Optimizer::evaluateConstant(const ExprNode* expr, bool& is_constant) {
    is_constant = false;
    
    if (auto* num = dynamic_cast<const NumberExprNode*>(expr)) {
        is_constant = true;
        return num->value;
    }
    
    if (auto* bin = dynamic_cast<const BinaryExprNode*>(expr)) {
        bool left_const = false, right_const = false;
        int left_val = evaluateConstant(bin->left.get(), left_const);
        int right_val = evaluateConstant(bin->right.get(), right_const);
        
        if (left_const && right_const) {
            is_constant = true;
            switch (bin->op) {
                case TokenType::PLUS: return left_val + right_val;
                case TokenType::MINUS: return left_val - right_val;
                case TokenType::MULTIPLY: return left_val * right_val;
                case TokenType::DIVIDE: 
                    if (right_val != 0) return left_val / right_val;
                    is_constant = false;
                    return 0;
                case TokenType::AMPERSAND: return left_val & right_val;
                case TokenType::PIPE: return left_val | right_val;
                case TokenType::CARET: return left_val ^ right_val;
                case TokenType::MODULO:
                    if (right_val != 0) return (left_val % right_val) & 0xFF;
                    is_constant = false;
                    return 0;
                case TokenType::SHIFT_LEFT:
                    return (right_val >= 8) ? 0 : ((left_val << right_val) & 0xFF);
                case TokenType::SHIFT_RIGHT:
                    return (right_val >= 8) ? 0 : ((left_val & 0xFF) >> right_val);
                case TokenType::EQUALS: return left_val == right_val ? 1 : 0;
                case TokenType::NOT_EQUALS: return left_val != right_val ? 1 : 0;
                case TokenType::LESS_THAN: return left_val < right_val ? 1 : 0;
                case TokenType::LESS_EQUAL: return left_val <= right_val ? 1 : 0;
                case TokenType::GREATER_THAN: return left_val > right_val ? 1 : 0;
                case TokenType::GREATER_EQUAL: return left_val >= right_val ? 1 : 0;
                default:
                    is_constant = false;
                    return 0;
            }
        }
    }
    
    // Check for condition nodes too (they're also expressions)
    if (auto* cond = dynamic_cast<const ConditionNode*>(expr)) {
        bool left_const = false, right_const = false;
        int left_val = evaluateConstant(cond->left.get(), left_const);
        int right_val = evaluateConstant(cond->right.get(), right_const);
        
        if (left_const && right_const) {
            is_constant = true;
            switch (cond->op) {
                case TokenType::EQUALS: return left_val == right_val ? 1 : 0;
                case TokenType::NOT_EQUALS: return left_val != right_val ? 1 : 0;
                case TokenType::LESS_THAN: return left_val < right_val ? 1 : 0;
                case TokenType::LESS_EQUAL: return left_val <= right_val ? 1 : 0;
                case TokenType::GREATER_THAN: return left_val > right_val ? 1 : 0;
                case TokenType::GREATER_EQUAL: return left_val >= right_val ? 1 : 0;
                default:
                    is_constant = false;
                    return 0;
            }
        }
    }
    
    return 0;
}

std::unique_ptr<ExprNode> Optimizer::foldExpression(std::unique_ptr<ExprNode> expr) {
    if (!fold_constants || !expr) return expr;
    
    // Recursively fold subexpressions first
    if (auto* bin = dynamic_cast<BinaryExprNode*>(expr.get())) {
        bin->left = foldExpression(std::move(bin->left));
        bin->right = foldExpression(std::move(bin->right));
    }
    
    // Try to evaluate as constant
    bool is_constant = false;
    int value = evaluateConstant(expr.get(), is_constant);
    
    if (is_constant) {
        constants_folded++;
        auto folded = std::make_unique<NumberExprNode>();
        folded->value = value;
        folded->line = expr->line;
        folded->column = expr->column;
        return folded;
    }
    
    return expr;
}

bool Optimizer::isDeadCode(const ASTNode* stmt, bool after_return) {
    if (after_return) return true;
    return false;
}

void Optimizer::eliminateDeadCode(BlockNode& block) {
    if (!eliminate_dead_code) return;
    
    bool after_return = false;
    auto it = block.statements.begin();
    
    while (it != block.statements.end()) {
        // Fold expressions in statements
        if (fold_constants) {
            if (auto* var_decl = dynamic_cast<VariableDeclNode*>(it->get())) {
                if (var_decl->initializer) {
                    var_decl->initializer = foldExpression(std::move(var_decl->initializer));
                }
            } else if (auto* assign = dynamic_cast<AssignmentNode*>(it->get())) {
                assign->value = foldExpression(std::move(assign->value));
            } else if (auto* if_stmt = dynamic_cast<IfNode*>(it->get())) {
                if_stmt->condition = foldExpression(std::move(if_stmt->condition));
                if (auto* then_block = dynamic_cast<BlockNode*>(if_stmt->then_branch.get())) {
                    eliminateDeadCode(*then_block);
                }
                if (auto* else_block = dynamic_cast<BlockNode*>(if_stmt->else_branch.get())) {
                    eliminateDeadCode(*else_block);
                }
            } else if (auto* while_stmt = dynamic_cast<WhileNode*>(it->get())) {
                while_stmt->condition = foldExpression(std::move(while_stmt->condition));
                if (auto* body_block = dynamic_cast<BlockNode*>(while_stmt->body.get())) {
                    eliminateDeadCode(*body_block);
                }
            }
        }
        
        if (isDeadCode(it->get(), after_return)) {
            dead_code_removed++;
            it = block.statements.erase(it);
        } else {
            if (dynamic_cast<ReturnNode*>(it->get())) {
                after_return = true;
            }
            ++it;
        }
    }
}

void Optimizer::optimize(ProgramNode& program) {
    constants_folded = 0;
    dead_code_removed = 0;
    
    for (auto& node : program.functions) {
        if (auto* func = dynamic_cast<FunctionNode*>(node.get())) {
            if (auto* body = dynamic_cast<BlockNode*>(func->body.get())) {
                eliminateDeadCode(*body);
            }
        }
    }
}

} // namespace compiler
} // namespace chip8
