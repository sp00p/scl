#include <chip8/compiler/parser.h>
#include <stdexcept>

namespace chip8::compiler {

// Deep copy of an expression tree. Needed when an expression must be evaluated
// twice, e.g. the index in "arr[i+1] += 2" is used for both the read and the write.
static std::unique_ptr<ExprNode> clone_expr(const ExprNode* expr) {
    if (!expr) return nullptr;

    std::unique_ptr<ExprNode> copy;
    if (auto* n = dynamic_cast<const NumberExprNode*>(expr)) {
        auto c = std::make_unique<NumberExprNode>();
        c->value = n->value;
        copy = std::move(c);
    } else if (auto* v = dynamic_cast<const VariableExprNode*>(expr)) {
        auto c = std::make_unique<VariableExprNode>();
        c->name = v->name;
        copy = std::move(c);
    } else if (auto* s = dynamic_cast<const StringExprNode*>(expr)) {
        auto c = std::make_unique<StringExprNode>();
        c->value = s->value;
        copy = std::move(c);
    } else if (auto* b = dynamic_cast<const BinaryExprNode*>(expr)) {
        auto c = std::make_unique<BinaryExprNode>();
        c->op = b->op;
        c->left = clone_expr(b->left.get());
        c->right = clone_expr(b->right.get());
        copy = std::move(c);
    } else if (auto* cond = dynamic_cast<const ConditionNode*>(expr)) {
        auto c = std::make_unique<ConditionNode>();
        c->op = cond->op;
        c->left = clone_expr(cond->left.get());
        c->right = clone_expr(cond->right.get());
        copy = std::move(c);
    } else if (auto* l = dynamic_cast<const LogicalExprNode*>(expr)) {
        auto c = std::make_unique<LogicalExprNode>();
        c->op = l->op;
        c->left = clone_expr(l->left.get());
        c->right = clone_expr(l->right.get());
        copy = std::move(c);
    } else if (auto* u = dynamic_cast<const UnaryExprNode*>(expr)) {
        auto c = std::make_unique<UnaryExprNode>();
        c->op = u->op;
        c->operand = clone_expr(u->operand.get());
        copy = std::move(c);
    } else if (auto* k = dynamic_cast<const KeyExprNode*>(expr)) {
        auto c = std::make_unique<KeyExprNode>();
        c->key_num = clone_expr(k->key_num.get());
        copy = std::move(c);
    } else if (auto* r = dynamic_cast<const RandExprNode*>(expr)) {
        auto c = std::make_unique<RandExprNode>();
        c->max_val = clone_expr(r->max_val.get());
        copy = std::move(c);
    } else if (dynamic_cast<const WaitKeyExprNode*>(expr)) {
        copy = std::make_unique<WaitKeyExprNode>();
    } else if (dynamic_cast<const CollisionExprNode*>(expr)) {
        copy = std::make_unique<CollisionExprNode>();
    } else if (dynamic_cast<const TimerExprNode*>(expr)) {
        copy = std::make_unique<TimerExprNode>();
    } else if (auto* a = dynamic_cast<const ArrayAccessExprNode*>(expr)) {
        auto c = std::make_unique<ArrayAccessExprNode>();
        c->array_name = a->array_name;
        for (const auto& idx : a->indices) c->indices.push_back(clone_expr(idx.get()));
        copy = std::move(c);
    } else if (auto* f = dynamic_cast<const FunctionCallExprNode*>(expr)) {
        auto c = std::make_unique<FunctionCallExprNode>();
        c->function_name = f->function_name;
        for (const auto& arg : f->arguments) c->arguments.push_back(clone_expr(arg.get()));
        copy = std::move(c);
    } else if (auto* e = dynamic_cast<const EntityFieldAccessExpr*>(expr)) {
        auto c = std::make_unique<EntityFieldAccessExpr>();
        c->entity_name = e->entity_name;
        c->field_name = e->field_name;
        c->index = clone_expr(e->index.get());
        copy = std::move(c);
    } else {
        throw std::runtime_error("Internal error: cannot clone expression node");
    }

    copy->line = expr->line;
    copy->column = expr->column;
    return copy;
}

Parser::Parser(chip8::compiler::ErrorHandler &errorHandler)
    : tokens(nullptr), current_token(0), errorHandler(errorHandler) {
}

std::unique_ptr<ProgramNode> Parser::parse(const std::vector<Token>& tokens1) {
    this->tokens = &tokens1;
    current_token = 0;

    auto program = std::make_unique<ProgramNode>();

    try {
        while (!check(TokenType::END_OF_FILE)) {
            if (check(TokenType::SPRITE)) {
                program->functions.push_back(std::move(parse_sprite_definition()));
            } else if (check(TokenType::CONST)) {
                program->functions.push_back(std::move(parse_const_declaration()));
            } else if (check(TokenType::ENUM)) {
                program->functions.push_back(std::move(parse_enum_declaration()));
            } else if (check(TokenType::GLOBAL)) {
                program->functions.push_back(std::move(parse_global_var_declaration()));
            } else if (check(TokenType::ENTITY)) {
                program->functions.push_back(std::move(parse_entity_definition()));
            } else {
                program->functions.push_back(std::move(parse_function()));
            }
        }
    } catch (const std::exception& e) {
        errorHandler.error(e.what(), current().line, current().column);
    }

    return program;
}

std::unique_ptr<FunctionNode> Parser::parse_function() {
    auto function = std::make_unique<FunctionNode>();

    function->line = current().line;
    function->column = current().column;

    if (check(TokenType::VOID)) {
        function->returns_value = false;
        advance();
    } else if (check(TokenType::BYTE)) {
        function->returns_value = true;
        advance();
    } else {
        error("Expected 'void' or 'byte' return type");
    }

    expect(TokenType::IDENTIFIER, "Expected function name after return type");
    function->name = tokens->at(current_token - 1).value;

    expect(TokenType::LPAREN, "Expected '(' after function name");

    if (!check(TokenType::RPAREN)) {
        do {
            expect(TokenType::BYTE, "Expected 'byte' in parameter declaration");
            expect(TokenType::IDENTIFIER, "Expected parameter name");
            FunctionParam param;
            param.name = tokens->at(current_token - 1).value;
            param.line = tokens->at(current_token - 1).line;
            param.column = tokens->at(current_token - 1).column;
            function->params.push_back(param);
        } while (check(TokenType::COMMA) && (advance(), true));
    }

    expect(TokenType::RPAREN, "Expected ')' after parameters");

    function->body = std::move(parse_block());

    return function;
}

std::unique_ptr<BlockNode> Parser::parse_block() {
    auto block = std::make_unique<BlockNode>();

    block->line = current().line;
    block->column = current().column;

    expect(TokenType::LBRACE, "Expected '{' at start of block");

    while(!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        block->statements.push_back(std::move(parse_statement()));
    }

    expect(TokenType::RBRACE, "Expected '}' at end of block");

    return block;
}

std::unique_ptr<ASTNode> Parser::parse_statement() {
    size_t statement_start = current_token;

    try {
        if (check(TokenType::BYTE)) {
            return parse_variable_declaration();
        }
        else if (check(TokenType::SPRITE)) {
            return parse_sprite_definition();
        }
        else if (check(TokenType::IDENTIFIER) && is_entity_type(current().value)) {
            std::string type_name = current().value;
            advance();
            return parse_entity_declaration(type_name);
        }
        else if (check(TokenType::IF)) {
            return parse_if_statement();
        }
        else if (check(TokenType::WHILE)) {
            return parse_while_statement();
        }
        else if (check(TokenType::FOR)) {
            return parse_for_statement();
        }
        else if (check(TokenType::DRAW)) {
            return parse_draw_statement();
        }
        else if (check(TokenType::CLEAR)) {
            advance();
            if (check(TokenType::LPAREN)) {
                advance();
                expect(TokenType::RPAREN, "Expected ')' after clear(");
            }
            expect(TokenType::SEMICOLON, "Expected ';' after clear");
            return std::make_unique<ClearNode>();
        }
        else if (check(TokenType::WAIT)) {
            return parse_wait_statement();
        }
        else if (check(TokenType::BEEP)) {
            return parse_beep_statement();
        }
        else if (check(TokenType::DRAWNUM)) {
            return parse_drawnum_statement();
        }
        else if (check(TokenType::BREAK)) {
            advance();
            expect(TokenType::SEMICOLON, "Expected ';' after break");
            return std::make_unique<BreakNode>();
        }
        else if (check(TokenType::CONTINUE)) {
            advance();
            expect(TokenType::SEMICOLON, "Expected ';' after continue");
            return std::make_unique<ContinueNode>();
        }
        else if (check(TokenType::RETURN)) {
            return parse_return_statement();
        }
        else if (check(TokenType::SWITCH)) {
            return parse_switch_statement();
        }
        else if (check(TokenType::IDENTIFIER)) {
            return parse_assignment_or_call();
        }

        error("Expected statement");
        return nullptr;
    } catch (const std::exception& e) {
        current_token = statement_start;
        throw;
    }
}

std::unique_ptr<ASTNode> Parser::parse_variable_declaration() {
    int line = current().line;
    int col = current().column;

    advance();
    expect(TokenType::IDENTIFIER, "Expected variable name after 'byte'");

    std::string name = tokens->at(current_token - 1).value;

    if (check(TokenType::LBRACKET)) {
        auto array_decl = std::make_unique<ArrayDeclNode>();
        array_decl->line = line;
        array_decl->column = col;
        array_decl->name = name;
        array_decl->size = 1;

        while (check(TokenType::LBRACKET)) {
            advance();
            expect(TokenType::NUMBER, "Expected array size");
            int dim = std::stoi(tokens->at(current_token - 1).value);
            array_decl->dimensions.push_back(dim);
            array_decl->size *= dim;
            expect(TokenType::RBRACKET, "Expected ']' after array size");
        }
        expect(TokenType::SEMICOLON, "Expected ';' after array declaration");

        return array_decl;
    }

    auto var_decl = std::make_unique<VariableDeclNode>();
    var_decl->line = line;
    var_decl->column = col;
    var_decl->name = name;

    if (check(TokenType::ASSIGN)) {
        advance();
        var_decl->initializer = parse_expression();
    }

    expect(TokenType::SEMICOLON, "Expected ';' after variable declaration");

    return var_decl;
}

std::unique_ptr<ASTNode> Parser::parse_assignment_or_call() {
    int line = current().line;
    int col = current().column;

    std::string name = current().value;
    advance();

    if (check(TokenType::LPAREN)) {
        return parse_function_call(name, line, col);
    }

    if (check(TokenType::LBRACKET)) {
        auto arr_assign = std::make_unique<ArrayAssignmentNode>();
        arr_assign->line = line;
        arr_assign->column = col;
        arr_assign->array_name = name;

        while (check(TokenType::LBRACKET)) {
            advance();
            arr_assign->indices.push_back(parse_expression());
            expect(TokenType::RBRACKET, "Expected ']' after array index");
        }

        if (check(TokenType::DOT)) {
            advance();
            expect(TokenType::IDENTIFIER, "Expected field name after '.'");
            std::string field_name = tokens->at(current_token - 1).value;
            
            auto field_assign = std::make_unique<EntityFieldAssignNode>();
            field_assign->line = line;
            field_assign->column = col;
            field_assign->entity_name = name;
            field_assign->field_name = field_name;
            field_assign->index = std::move(arr_assign->indices[0]);
            
            expect(TokenType::ASSIGN, "Expected '=' after entity field");
            field_assign->value = parse_expression();
            expect(TokenType::SEMICOLON, "Expected ';' after assignment");
            return field_assign;
        }

        if (check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) {
            bool is_increment = check(TokenType::PLUS_PLUS);
            advance();

            auto arr_ref = std::make_unique<ArrayAccessExprNode>();
            arr_ref->array_name = arr_assign->array_name;
            arr_ref->line = arr_assign->line;
            arr_ref->column = arr_assign->column;
            for (auto& idx : arr_assign->indices) {
                arr_ref->indices.push_back(clone_expr(idx.get()));
            }

            auto one = std::make_unique<NumberExprNode>();
            one->value = 1;

            auto bin_expr = std::make_unique<BinaryExprNode>();
            bin_expr->line = arr_assign->line;
            bin_expr->column = arr_assign->column;
            bin_expr->left = std::move(arr_ref);
            bin_expr->right = std::move(one);
            bin_expr->op = is_increment ? TokenType::PLUS : TokenType::MINUS;

            arr_assign->value = std::move(bin_expr);
            expect(TokenType::SEMICOLON, "Expected ';' after increment/decrement");
            return arr_assign;
        }

        TokenType compound_op = TokenType::ASSIGN;
        if (check(TokenType::PLUS_ASSIGN) || check(TokenType::MINUS_ASSIGN) ||
            check(TokenType::MULTIPLY_ASSIGN) || check(TokenType::DIVIDE_ASSIGN) ||
            check(TokenType::AND_ASSIGN) || check(TokenType::OR_ASSIGN) ||
            check(TokenType::XOR_ASSIGN)) {
            compound_op = current().type;
            advance();
        } else {
            expect(TokenType::ASSIGN, "Expected '=' or compound assignment after array access");
        }

        auto rhs = parse_expression();

        if (compound_op != TokenType::ASSIGN) {
            auto arr_ref = std::make_unique<ArrayAccessExprNode>();
            arr_ref->array_name = arr_assign->array_name;
            arr_ref->line = arr_assign->line;
            arr_ref->column = arr_assign->column;
            for (auto& idx : arr_assign->indices) {
                arr_ref->indices.push_back(clone_expr(idx.get()));
            }

            auto bin_expr = std::make_unique<BinaryExprNode>();
            bin_expr->line = arr_assign->line;
            bin_expr->column = arr_assign->column;
            bin_expr->left = std::move(arr_ref);
            bin_expr->right = std::move(rhs);

            switch (compound_op) {
                case TokenType::PLUS_ASSIGN: bin_expr->op = TokenType::PLUS; break;
                case TokenType::MINUS_ASSIGN: bin_expr->op = TokenType::MINUS; break;
                case TokenType::MULTIPLY_ASSIGN: bin_expr->op = TokenType::MULTIPLY; break;
                case TokenType::DIVIDE_ASSIGN: bin_expr->op = TokenType::DIVIDE; break;
                case TokenType::AND_ASSIGN: bin_expr->op = TokenType::AMPERSAND; break;
                case TokenType::OR_ASSIGN: bin_expr->op = TokenType::PIPE; break;
                case TokenType::XOR_ASSIGN: bin_expr->op = TokenType::CARET; break;
                default: break;
            }
            arr_assign->value = std::move(bin_expr);
        } else {
            arr_assign->value = std::move(rhs);
        }

        expect(TokenType::SEMICOLON, "Expected ';' after assignment");
        return arr_assign;
    }

    if (check(TokenType::DOT)) {
        advance();
        expect(TokenType::IDENTIFIER, "Expected field name after '.'");
        std::string field_name = tokens->at(current_token - 1).value;
        
        auto field_assign = std::make_unique<EntityFieldAssignNode>();
        field_assign->line = line;
        field_assign->column = col;
        field_assign->entity_name = name;
        field_assign->field_name = field_name;
        
        expect(TokenType::ASSIGN, "Expected '=' after entity field");
        field_assign->value = parse_expression();
        expect(TokenType::SEMICOLON, "Expected ';' after assignment");
        return field_assign;
    }

    if (check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) {
        bool is_increment = check(TokenType::PLUS_PLUS);
        advance();

        auto assignment = std::make_unique<AssignmentNode>();
        assignment->line = line;
        assignment->column = col;
        assignment->name = name;

        auto var_ref = std::make_unique<VariableExprNode>();
        var_ref->name = name;
        var_ref->line = line;
        var_ref->column = col;

        auto one = std::make_unique<NumberExprNode>();
        one->value = 1;
        one->line = line;
        one->column = col;

        auto bin_expr = std::make_unique<BinaryExprNode>();
        bin_expr->line = line;
        bin_expr->column = col;
        bin_expr->left = std::move(var_ref);
        bin_expr->right = std::move(one);
        bin_expr->op = is_increment ? TokenType::PLUS : TokenType::MINUS;

        assignment->value = std::move(bin_expr);
        expect(TokenType::SEMICOLON, "Expected ';' after increment/decrement");
        return assignment;
    }

    auto assignment = std::make_unique<AssignmentNode>();
    assignment->line = line;
    assignment->column = col;
    assignment->name = name;

    TokenType compound_op = TokenType::ASSIGN;
    if (check(TokenType::PLUS_ASSIGN) || check(TokenType::MINUS_ASSIGN) ||
        check(TokenType::MULTIPLY_ASSIGN) || check(TokenType::DIVIDE_ASSIGN) ||
        check(TokenType::AND_ASSIGN) || check(TokenType::OR_ASSIGN) ||
        check(TokenType::XOR_ASSIGN)) {
        compound_op = current().type;
        advance();
    } else {
        expect(TokenType::ASSIGN, "Expected '=' or compound assignment in assignment");
    }

    auto rhs = parse_expression();
    if (!rhs) {
        error("Expected expression in assignment");
    }

    if (compound_op != TokenType::ASSIGN) {
        auto var_ref = std::make_unique<VariableExprNode>();
        var_ref->name = assignment->name;
        var_ref->line = assignment->line;
        var_ref->column = assignment->column;

        auto bin_expr = std::make_unique<BinaryExprNode>();
        bin_expr->line = assignment->line;
        bin_expr->column = assignment->column;
        bin_expr->left = std::move(var_ref);
        bin_expr->right = std::move(rhs);

        switch (compound_op) {
            case TokenType::PLUS_ASSIGN: bin_expr->op = TokenType::PLUS; break;
            case TokenType::MINUS_ASSIGN: bin_expr->op = TokenType::MINUS; break;
            case TokenType::MULTIPLY_ASSIGN: bin_expr->op = TokenType::MULTIPLY; break;
            case TokenType::DIVIDE_ASSIGN: bin_expr->op = TokenType::DIVIDE; break;
            case TokenType::AND_ASSIGN: bin_expr->op = TokenType::AMPERSAND; break;
            case TokenType::OR_ASSIGN: bin_expr->op = TokenType::PIPE; break;
            case TokenType::XOR_ASSIGN: bin_expr->op = TokenType::CARET; break;
            default: break;
        }
        assignment->value = std::move(bin_expr);
    } else {
        assignment->value = std::move(rhs);
    }

    expect(TokenType::SEMICOLON, "Expected ';' after assignment");

    return assignment;
}

std::unique_ptr<ReturnNode> Parser::parse_return_statement() {
    auto ret = std::make_unique<ReturnNode>();
    ret->line = current().line;
    ret->column = current().column;

    advance();

    if (!check(TokenType::SEMICOLON)) {
        ret->value = parse_expression();
    }

    expect(TokenType::SEMICOLON, "Expected ';' after return");
    return ret;
}

std::unique_ptr<FunctionCallNode> Parser::parse_function_call(const std::string& name, int line, int col) {
    auto call = std::make_unique<FunctionCallNode>();
    call->line = line;
    call->column = col;
    call->function_name = name;

    expect(TokenType::LPAREN, "Expected '(' after function name");

    if (!check(TokenType::RPAREN)) {
        do {
            call->arguments.push_back(parse_expression());
        } while (check(TokenType::COMMA) && (advance(), true));
    }

    expect(TokenType::RPAREN, "Expected ')' after arguments");
    expect(TokenType::SEMICOLON, "Expected ';' after function call");

    return call;
}

std::unique_ptr<ExprNode> Parser::parse_primary_expression() {
    int line = current().line;
    int col = current().column;

    if (check(TokenType::LPAREN)) {
        advance();
        // Full condition grammar inside parens so both (x + 1) and (a < b) work
        auto expr = parse_condition();
        expect(TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }

    if (check(TokenType::NUMBER)) {
        auto num = std::make_unique<NumberExprNode>();
        num->line = line;
        num->column = col;
        num->value = std::stoi(current().value);
        advance();
        return num;
    }
    if (check(TokenType::HEX_NUMBER)) {
        auto num = std::make_unique<NumberExprNode>();
        num->line = line;
        num->column = col;
        num->value = std::stoi(current().value, nullptr, 16);
        advance();
        return num;
    }
    if (check(TokenType::STRING)) {
        auto str = std::make_unique<StringExprNode>();
        str->line = line;
        str->column = col;
        str->value = current().value;
        advance();
        return str;
    }
    if (check(TokenType::KEY)) {
        advance();
        expect(TokenType::LPAREN, "Expected '(' after key");
        auto key_expr = std::make_unique<KeyExprNode>();
        key_expr->line = line;
        key_expr->column = col;
        key_expr->key_num = parse_expression();
        expect(TokenType::RPAREN, "Expected ')' after key number");
        return key_expr;
    }
    if (check(TokenType::WAITKEY)) {
        advance();
        expect(TokenType::LPAREN, "Expected '(' after waitkey");
        expect(TokenType::RPAREN, "Expected ')' after waitkey");
        auto wk = std::make_unique<WaitKeyExprNode>();
        wk->line = line;
        wk->column = col;
        return wk;
    }
    if (check(TokenType::RAND)) {
        advance();
        expect(TokenType::LPAREN, "Expected '(' after rand");
        auto rand_expr = std::make_unique<RandExprNode>();
        rand_expr->line = line;
        rand_expr->column = col;
        rand_expr->max_val = parse_expression();
        expect(TokenType::RPAREN, "Expected ')' after rand max");
        return rand_expr;
    }
    if (check(TokenType::COLLISION)) {
        advance();
        auto coll = std::make_unique<CollisionExprNode>();
        coll->line = line;
        coll->column = col;
        return coll;
    }
    if (check(TokenType::TIMER)) {
        advance();
        auto timer_expr = std::make_unique<TimerExprNode>();
        timer_expr->line = line;
        timer_expr->column = col;
        return timer_expr;
    }
    if (check(TokenType::NOT)) {
        advance();
        auto unary = std::make_unique<UnaryExprNode>();
        unary->line = line;
        unary->column = col;
        unary->op = TokenType::NOT;
        unary->operand = parse_primary_expression();
        return unary;
    }
    if (check(TokenType::IDENTIFIER)) {
        std::string name = current().value;
        advance();

        if (check(TokenType::LPAREN)) {
            advance();
            auto call = std::make_unique<FunctionCallExprNode>();
            call->line = line;
            call->column = col;
            call->function_name = name;

            if (!check(TokenType::RPAREN)) {
                do {
                    call->arguments.push_back(parse_expression());
                } while (check(TokenType::COMMA) && (advance(), true));
            }
            expect(TokenType::RPAREN, "Expected ')' after arguments");
            return call;
        }

        std::unique_ptr<ExprNode> index = nullptr;
        if (check(TokenType::LBRACKET)) {
            advance();
            index = parse_expression();
            expect(TokenType::RBRACKET, "Expected ']' after index");
        }
        
        if (check(TokenType::DOT)) {
            advance();
            expect(TokenType::IDENTIFIER, "Expected field name after '.'");
            std::string field_name = tokens->at(current_token - 1).value;
            
            auto field_access = std::make_unique<EntityFieldAccessExpr>();
            field_access->line = line;
            field_access->column = col;
            field_access->entity_name = name;
            field_access->field_name = field_name;
            field_access->index = std::move(index);
            return field_access;
        }
        
        if (index) {
            auto arr_access = std::make_unique<ArrayAccessExprNode>();
            arr_access->line = line;
            arr_access->column = col;
            arr_access->array_name = name;
            arr_access->indices.push_back(std::move(index));
            
            while (check(TokenType::LBRACKET)) {
                advance();
                arr_access->indices.push_back(parse_expression());
                expect(TokenType::RBRACKET, "Expected ']' after array index");
            }
            return arr_access;
        }

        auto var = std::make_unique<VariableExprNode>();
        var->line = line;
        var->column = col;
        var->name = name;
        return var;
    }

    error("Expected expression");
    return nullptr;
}

// Expression grammar with C-style operator precedence (lowest to highest):
//   |  ^  &  + -  * /  primary
std::unique_ptr<ExprNode> Parser::parse_expression() {
    return parse_bitwise_or();
}

// Helper to build a left-associative binary operator level
static std::unique_ptr<ExprNode> make_binary(std::unique_ptr<ExprNode> left,
                                             TokenType op, int line, int col,
                                             std::unique_ptr<ExprNode> right) {
    auto bin_expr = std::make_unique<BinaryExprNode>();
    bin_expr->line = line;
    bin_expr->column = col;
    bin_expr->left = std::move(left);
    bin_expr->op = op;
    bin_expr->right = std::move(right);
    return bin_expr;
}

std::unique_ptr<ExprNode> Parser::parse_bitwise_or() {
    auto left = parse_bitwise_xor();
    while (check(TokenType::PIPE)) {
        int line = current().line, col = current().column;
        advance();
        left = make_binary(std::move(left), TokenType::PIPE, line, col, parse_bitwise_xor());
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_bitwise_xor() {
    auto left = parse_bitwise_and();
    while (check(TokenType::CARET)) {
        int line = current().line, col = current().column;
        advance();
        left = make_binary(std::move(left), TokenType::CARET, line, col, parse_bitwise_and());
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_bitwise_and() {
    auto left = parse_additive();
    while (check(TokenType::AMPERSAND)) {
        int line = current().line, col = current().column;
        advance();
        left = make_binary(std::move(left), TokenType::AMPERSAND, line, col, parse_additive());
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_additive() {
    auto left = parse_multiplicative();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        TokenType op = current().type;
        int line = current().line, col = current().column;
        advance();
        left = make_binary(std::move(left), op, line, col, parse_multiplicative());
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_multiplicative() {
    auto left = parse_primary_expression();
    while (check(TokenType::MULTIPLY) || check(TokenType::DIVIDE)) {
        TokenType op = current().type;
        int line = current().line, col = current().column;
        advance();
        left = make_binary(std::move(left), op, line, col, parse_primary_expression());
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parse_comparison() {
    auto condition = std::make_unique<ConditionNode>();

    condition->line = current().line;
    condition->column = current().column;

    condition->left = parse_expression();

    if (check(TokenType::EQUALS) || check(TokenType::NOT_EQUALS) ||
        check(TokenType::LESS_THAN) || check(TokenType::GREATER_THAN) ||
        check(TokenType::LESS_EQUAL) || check(TokenType::GREATER_EQUAL)) {
        condition->op = current().type;
        advance();

        condition->right = parse_expression();
        return condition;
    }

    // No comparison operator: return the bare expression. Contexts that
    // branch (if/while/&&/||) already test for truthiness (value != 0).
    return std::unique_ptr<ExprNode>(condition->left.release());
}

std::unique_ptr<ExprNode> Parser::parse_condition() {
    auto left = parse_comparison();

    while (check(TokenType::AND_AND) || check(TokenType::OR_OR)) {
        auto logical = std::make_unique<LogicalExprNode>();
        logical->line = current().line;
        logical->column = current().column;
        logical->op = current().type;
        advance();

        logical->left = std::move(left);
        logical->right = parse_comparison();
        left = std::move(logical);
    }

    return left;
}

std::unique_ptr<IfNode> Parser::parse_if_statement() {
    auto if_node = std::make_unique<IfNode>();

    if_node->line = current().line;
    if_node->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after if");

    if_node->condition = parse_condition();

    expect(TokenType::RPAREN, "Expected ')' after condition");

    if_node->then_branch = std::move(parse_block());

    if (check(TokenType::ELSE)) {
        advance();
        if (check(TokenType::IF)) {
            if_node->else_branch = parse_if_statement();
        } else {
            if_node->else_branch = std::move(parse_block());
        }
    }

    return if_node;
}

std::unique_ptr<SwitchNode> Parser::parse_switch_statement() {
    auto switch_node = std::make_unique<SwitchNode>();
    switch_node->line = current().line;
    switch_node->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after switch");

    switch_node->expr = parse_expression();

    expect(TokenType::RPAREN, "Expected ')' after switch expression");
    expect(TokenType::LBRACE, "Expected '{' after switch");

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        SwitchCase sw_case;

        if (check(TokenType::CASE)) {
            advance();
            sw_case.is_default = false;

            if (check(TokenType::NUMBER)) {
                sw_case.value = std::stoi(current().value);
                advance();
            } else if (check(TokenType::HEX_NUMBER)) {
                sw_case.value = std::stoi(current().value, nullptr, 16);
                advance();
            } else {
                error("Expected number after 'case'");
            }

            expect(TokenType::COLON, "Expected ':' after case value");
        } else if (check(TokenType::DEFAULT)) {
            advance();
            sw_case.is_default = true;
            sw_case.value = -1;
            expect(TokenType::COLON, "Expected ':' after 'default'");
        } else {
            error("Expected 'case' or 'default' in switch");
        }

        while (!check(TokenType::CASE) && !check(TokenType::DEFAULT) && 
               !check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
            sw_case.statements.push_back(parse_statement());
        }

        switch_node->cases.push_back(std::move(sw_case));
    }

    expect(TokenType::RBRACE, "Expected '}' at end of switch");

    return switch_node;
}

std::unique_ptr<WhileNode> Parser::parse_while_statement() {
    auto while_node = std::make_unique<WhileNode>();

    while_node->line = current().line;
    while_node->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after while");

    while_node->condition = parse_condition();

    expect(TokenType::RPAREN, "Expected ')' after condition");

    while_node->body = std::move(parse_block());

    return while_node;
}

std::unique_ptr<DrawNode> Parser::parse_draw_statement() {
    auto draw = std::make_unique<DrawNode>();

    draw->line = current().line;
    draw->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after draw");

    draw->x_expr = parse_expression();

    expect(TokenType::COMMA, "Expected ',' between coordinates");

    draw->y_expr = parse_expression();

    expect(TokenType::COMMA, "Expected ',' after y-coordinate");

    if (check(TokenType::NUMBER)) {
        draw->height = std::stoi(current().value) & 0xF;
        advance();

        expect(TokenType::COMMA, "Expected ',' after height");

        if (check(TokenType::NUMBER) || check(TokenType::HEX_NUMBER)) {
            draw->sprite_id = std::stoi(current().value, nullptr, check(TokenType::HEX_NUMBER) ? 16 : 10) & 0xF;
            draw->sprite_name = "";
            advance();
        } else if (check(TokenType::IDENTIFIER)) {
            draw->sprite_name = current().value;
            draw->sprite_id = -1;
            advance();
        } else {
            error("Expected sprite identifier or name");
        }
    } else if (check(TokenType::IDENTIFIER)) {
        draw->sprite_name = current().value;
        draw->sprite_id = -1;
        draw->height = 0;
        advance();
    } else {
        error("Expected sprite height or name");
    }

    expect(TokenType::RPAREN, "Expected ')' after draw arguments");
    expect(TokenType::SEMICOLON, "Expected ';' after draw statement");

    return draw;
}

std::unique_ptr<WaitNode> Parser::parse_wait_statement() {
    auto wait = std::make_unique<WaitNode>();

    wait->line = current().line;
    wait->column = current().column;

    advance();

    if (match(TokenType::LPAREN)) {
        if (check(TokenType::NUMBER)) {
            wait->type = WaitNode::WaitType::FIXED;
            wait->fixed_duration = std::stoi(current().value);
            advance();
        }
        else if (check(TokenType::IDENTIFIER)) {
            wait->type = WaitNode::WaitType::VARIABLE;
            auto var = std::make_unique<VariableExprNode>();
            var->name = current().value;
            advance();
            wait->duration = std::move(var);
        }
        else {
            error("Expected number or variable after wait(");
        }

        expect(TokenType::RPAREN, "Expected ')' after wait value");
    }
    else {
        wait->type = WaitNode::WaitType::FIXED;
        wait->fixed_duration = 15;
    }

    expect(TokenType::SEMICOLON, "Expected ';' after wait statement");

    return wait;
}

const Token& Parser::current() {
    if (current_token >= tokens->size()) {
        error("Unexpected end of file");
    }
    return (*tokens)[current_token];
}

const Token& Parser::peek() {
    if (current_token + 1 >= tokens->size()) {
        error("Unexpected end of file");
    }
    return (*tokens)[current_token + 1];
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

void Parser::advance() {
    if (current_token < tokens->size()) {
        current_token++;
    }
}

bool Parser::check(TokenType type) {
    if (current_token >= tokens->size()) {
        return false;
    }
    return current().type == type;
}

void Parser::expect(TokenType type, const std::string& message) {
    if (!match(type)) {
        error(message);
    }
}

void Parser::error(const std::string& message) {
    std::string error_msg = "Error at line " + std::to_string(current().line) +
                            ", column " + std::to_string(current().column) +
                            ": " + message;
    throw std::runtime_error(error_msg);
}

std::unique_ptr<BeepNode> Parser::parse_beep_statement() {
    auto beep = std::make_unique<BeepNode>();
    beep->line = current().line;
    beep->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after beep");
    beep->duration = parse_primary_expression();
    expect(TokenType::RPAREN, "Expected ')' after beep duration");
    expect(TokenType::SEMICOLON, "Expected ';' after beep statement");

    return beep;
}

std::unique_ptr<DrawNumNode> Parser::parse_drawnum_statement() {
    auto drawnum = std::make_unique<DrawNumNode>();
    drawnum->line = current().line;
    drawnum->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after drawnum");

    drawnum->value = parse_expression();

    expect(TokenType::COMMA, "Expected ',' after value");

    drawnum->x_expr = parse_expression();

    expect(TokenType::COMMA, "Expected ',' after x");

    drawnum->y_expr = parse_expression();

    expect(TokenType::RPAREN, "Expected ')' after drawnum arguments");
    expect(TokenType::SEMICOLON, "Expected ';' after drawnum statement");

    return drawnum;
}

std::unique_ptr<SpriteDefNode> Parser::parse_sprite_definition() {
    auto sprite = std::make_unique<SpriteDefNode>();
    sprite->line = current().line;
    sprite->column = current().column;

    advance();
    expect(TokenType::IDENTIFIER, "Expected sprite name");
    sprite->name = tokens->at(current_token - 1).value;

    expect(TokenType::LBRACKET, "Expected '[' after sprite name");
    expect(TokenType::NUMBER, "Expected sprite height");
    sprite->height = std::stoi(tokens->at(current_token - 1).value);
    expect(TokenType::RBRACKET, "Expected ']' after height");

    expect(TokenType::ASSIGN, "Expected '=' before sprite data");
    expect(TokenType::LBRACE, "Expected '{' before sprite bytes");

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        if (check(TokenType::NUMBER)) {
            sprite->data.push_back(static_cast<uint8_t>(std::stoi(current().value)));
            advance();
        } else if (check(TokenType::HEX_NUMBER)) {
            sprite->data.push_back(static_cast<uint8_t>(std::stoi(current().value, nullptr, 16)));
            advance();
        } else if (check(TokenType::STRING)) {
            const std::string& row = current().value;
            uint8_t byte = 0;
            for (size_t j = 0; j < 8 && j < row.size(); j++) {
                char ch = row[j];
                if (ch == 'X' || ch == '#' || ch == '1' || ch == '*') {
                    byte |= (0x80 >> j);
                }
            }
            sprite->data.push_back(byte);
            advance();
        } else {
            error("Expected number or string in sprite data");
        }

        if (check(TokenType::COMMA)) {
            advance();
        } else if (!check(TokenType::RBRACE)) {
            error("Expected ',' or '}' in sprite data");
        }
    }

    expect(TokenType::RBRACE, "Expected '}' after sprite data");
    expect(TokenType::SEMICOLON, "Expected ';' after sprite definition");

    return sprite;
}

std::unique_ptr<ForNode> Parser::parse_for_statement() {
    auto for_node = std::make_unique<ForNode>();
    for_node->line = current().line;
    for_node->column = current().column;

    advance();
    expect(TokenType::LPAREN, "Expected '(' after for");

    if (check(TokenType::BYTE)) {
        for_node->init = parse_variable_declaration();
    } else if (check(TokenType::IDENTIFIER)) {
        auto assignment = std::make_unique<AssignmentNode>();
        assignment->line = current().line;
        assignment->column = current().column;
        assignment->name = current().value;
        advance();
        expect(TokenType::ASSIGN, "Expected '=' in for init");
        assignment->value = parse_expression();
        expect(TokenType::SEMICOLON, "Expected ';' after for init");
        for_node->init = std::move(assignment);
    } else {
        expect(TokenType::SEMICOLON, "Expected init statement or ';'");
    }

    for_node->condition = parse_condition();
    expect(TokenType::SEMICOLON, "Expected ';' after for condition");

    if (check(TokenType::IDENTIFIER)) {
        auto assignment = std::make_unique<AssignmentNode>();
        assignment->line = current().line;
        assignment->column = current().column;
        assignment->name = current().value;
        advance();

        // i++ / i-- shorthand: desugar to i = i +/- 1
        if (check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) {
            bool is_increment = check(TokenType::PLUS_PLUS);
            advance();

            auto var_ref = std::make_unique<VariableExprNode>();
            var_ref->name = assignment->name;
            var_ref->line = assignment->line;
            var_ref->column = assignment->column;

            auto one = std::make_unique<NumberExprNode>();
            one->value = 1;

            auto bin_expr = std::make_unique<BinaryExprNode>();
            bin_expr->line = assignment->line;
            bin_expr->column = assignment->column;
            bin_expr->left = std::move(var_ref);
            bin_expr->right = std::move(one);
            bin_expr->op = is_increment ? TokenType::PLUS : TokenType::MINUS;

            assignment->value = std::move(bin_expr);
            for_node->increment = std::move(assignment);
            expect(TokenType::RPAREN, "Expected ')' after for");
            for_node->body = parse_block();
            return for_node;
        }

        TokenType compound_op = TokenType::ASSIGN;
        if (check(TokenType::PLUS_ASSIGN) || check(TokenType::MINUS_ASSIGN) ||
            check(TokenType::MULTIPLY_ASSIGN) || check(TokenType::DIVIDE_ASSIGN) ||
            check(TokenType::AND_ASSIGN) || check(TokenType::OR_ASSIGN) ||
            check(TokenType::XOR_ASSIGN)) {
            compound_op = current().type;
            advance();
        } else {
            expect(TokenType::ASSIGN, "Expected '=', '++', '--', or compound assignment in for-loop increment");
        }
        
        auto rhs = parse_expression();
        
        if (compound_op != TokenType::ASSIGN) {
            auto var_ref = std::make_unique<VariableExprNode>();
            var_ref->name = assignment->name;
            var_ref->line = assignment->line;
            var_ref->column = assignment->column;
            
            auto bin_expr = std::make_unique<BinaryExprNode>();
            bin_expr->line = assignment->line;
            bin_expr->column = assignment->column;
            bin_expr->left = std::move(var_ref);
            bin_expr->right = std::move(rhs);
            
            switch (compound_op) {
                case TokenType::PLUS_ASSIGN: bin_expr->op = TokenType::PLUS; break;
                case TokenType::MINUS_ASSIGN: bin_expr->op = TokenType::MINUS; break;
                case TokenType::MULTIPLY_ASSIGN: bin_expr->op = TokenType::MULTIPLY; break;
                case TokenType::DIVIDE_ASSIGN: bin_expr->op = TokenType::DIVIDE; break;
                case TokenType::AND_ASSIGN: bin_expr->op = TokenType::AMPERSAND; break;
                case TokenType::OR_ASSIGN: bin_expr->op = TokenType::PIPE; break;
                case TokenType::XOR_ASSIGN: bin_expr->op = TokenType::CARET; break;
                default: break;
            }
            assignment->value = std::move(bin_expr);
        } else {
            assignment->value = std::move(rhs);
        }
        
        for_node->increment = std::move(assignment);
    }

    expect(TokenType::RPAREN, "Expected ')' after for");
    for_node->body = parse_block();

    return for_node;
}

std::unique_ptr<ConstDeclNode> Parser::parse_const_declaration() {
    auto const_decl = std::make_unique<ConstDeclNode>();
    const_decl->line = current().line;
    const_decl->column = current().column;

    advance();
    expect(TokenType::IDENTIFIER, "Expected constant name after 'const'");
    const_decl->name = tokens->at(current_token - 1).value;

    expect(TokenType::ASSIGN, "Expected '=' after constant name");

    if (check(TokenType::NUMBER)) {
        const_decl->value = std::stoi(current().value);
        advance();
    } else if (check(TokenType::HEX_NUMBER)) {
        const_decl->value = std::stoi(current().value, nullptr, 16);
        advance();
    } else {
        error("Expected numeric value for constant");
    }

    expect(TokenType::SEMICOLON, "Expected ';' after constant declaration");
    return const_decl;
}

std::unique_ptr<EnumDeclNode> Parser::parse_enum_declaration() {
    auto enum_decl = std::make_unique<EnumDeclNode>();
    enum_decl->line = current().line;
    enum_decl->column = current().column;

    advance();
    expect(TokenType::IDENTIFIER, "Expected enum name after 'enum'");
    enum_decl->name = tokens->at(current_token - 1).value;

    expect(TokenType::LBRACE, "Expected '{' after enum name");

    int next_value = 0;
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        EnumValue ev;
        expect(TokenType::IDENTIFIER, "Expected enum value name");
        ev.name = tokens->at(current_token - 1).value;

        if (check(TokenType::ASSIGN)) {
            advance();
            if (check(TokenType::NUMBER)) {
                ev.value = std::stoi(current().value);
                advance();
            } else if (check(TokenType::HEX_NUMBER)) {
                ev.value = std::stoi(current().value, nullptr, 16);
                advance();
            } else {
                error("Expected numeric value for enum member");
            }
            next_value = ev.value + 1;
        } else {
            ev.value = next_value++;
        }

        enum_decl->values.push_back(ev);

        if (check(TokenType::COMMA)) {
            advance();
        } else if (!check(TokenType::RBRACE)) {
            error("Expected ',' or '}' in enum definition");
        }
    }

    expect(TokenType::RBRACE, "Expected '}' after enum values");

    return enum_decl;
}

std::unique_ptr<GlobalVarDeclNode> Parser::parse_global_var_declaration() {
    auto global_decl = std::make_unique<GlobalVarDeclNode>();
    global_decl->line = current().line;
    global_decl->column = current().column;

    advance();
    expect(TokenType::BYTE, "Expected 'byte' after 'global'");
    expect(TokenType::IDENTIFIER, "Expected variable name");
    global_decl->name = tokens->at(current_token - 1).value;

    if (check(TokenType::ASSIGN)) {
        advance();
        global_decl->initializer = parse_primary_expression();
    }

    expect(TokenType::SEMICOLON, "Expected ';' after global declaration");
    return global_decl;
}

bool Parser::is_entity_type(const std::string& name) {
    return entity_types.find(name) != entity_types.end();
}

std::unique_ptr<EntityDefNode> Parser::parse_entity_definition() {
    auto entity_def = std::make_unique<EntityDefNode>();
    entity_def->line = current().line;
    entity_def->column = current().column;

    advance();
    expect(TokenType::IDENTIFIER, "Expected entity name after 'entity'");
    entity_def->name = tokens->at(current_token - 1).value;

    expect(TokenType::LBRACE, "Expected '{' after entity name");

    int offset = 0;
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        expect(TokenType::BYTE, "Expected 'byte' in entity field declaration");
        expect(TokenType::IDENTIFIER, "Expected field name");
        
        EntityField field;
        field.name = tokens->at(current_token - 1).value;
        field.offset = offset;
        entity_def->fields.push_back(field);
        offset += 1;

        expect(TokenType::SEMICOLON, "Expected ';' after field declaration");
    }

    entity_def->size = offset;
    expect(TokenType::RBRACE, "Expected '}' after entity fields");

    entity_types[entity_def->name] = entity_def->fields;

    return entity_def;
}

std::unique_ptr<EntityDeclNode> Parser::parse_entity_declaration(const std::string& type_name) {
    auto entity_decl = std::make_unique<EntityDeclNode>();
    entity_decl->line = current().line;
    entity_decl->column = current().column;
    entity_decl->type_name = type_name;

    expect(TokenType::IDENTIFIER, "Expected variable name after entity type");
    entity_decl->var_name = tokens->at(current_token - 1).value;

    if (check(TokenType::LBRACKET)) {
        advance();
        expect(TokenType::NUMBER, "Expected array size");
        entity_decl->array_size = std::stoi(tokens->at(current_token - 1).value);
        expect(TokenType::RBRACKET, "Expected ']' after array size");
    } else {
        entity_decl->array_size = 0;
    }

    expect(TokenType::SEMICOLON, "Expected ';' after entity declaration");
    return entity_decl;
}

}
