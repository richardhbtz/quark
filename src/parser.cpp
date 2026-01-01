#include "../include/parser.h"
#include "../include/compilation_context.h"
#include "../include/error_reporter.h"
#include "../include/source_manager.h"
#include <stdexcept>
#include <utility>
#include <vector>
#include <memory>
#include <string>
#include <iostream>
#include <map>
#include <sstream>
#include <cstdlib>
#include <functional>
#include <fstream>

Parser::Parser(Lexer &lex, bool verbose, CompilationContext* ctx) 
    : lex_(lex), verbose_(verbose), ctx_(ctx)
{
    cur_ = lex_.peek();
    if (verbose_)
    {
        printf("[parser] first token kind=%d text='%s' number=%g at %d:%d\n", (int)cur_.kind, cur_.text.c_str(), cur_.numberValue, cur_.location.line, cur_.location.column);
    }
}

void Parser::next()
{
    if (hasPeeked_) {
        cur_ = peeked_;
        hasPeeked_ = false;
    } else {
        cur_ = lex_.next();
    }
}

Token Parser::peekToken()
{
    if (!hasPeeked_) {
        peeked_ = lex_.next();
        hasPeeked_ = true;
    }
    return peeked_;
}

ErrorReporter* Parser::errorReporter() const {
    if (ctx_) return &ctx_->errorReporter;
    return g_errorReporter.get();
}

SourceManager* Parser::sourceManager() const {
    if (ctx_) return &ctx_->sourceManager;
    return g_sourceManager.get();
}

void Parser::expect(TokenKind kind, const std::string &msg)
{
    if (cur_.kind != kind)
    {
        auto* er = errorReporter();
        auto* sm = sourceManager();
        if (er && sm) {
            auto file = sm->getFile(cur_.location.filename);
            if (file) {
                std::string errorCode = ErrorCodes::UNEXPECTED_TOKEN;
                if (msg.find("semicolon") != std::string::npos || msg.find("';'") != std::string::npos) {
                    errorCode = ErrorCodes::MISSING_SEMICOLON;
                } else if (msg.find("brace") != std::string::npos || msg.find("'{'") != std::string::npos || msg.find("'}'") != std::string::npos) {
                    errorCode = ErrorCodes::MISSING_BRACE;
                }
                throw EnhancedParseError(msg, cur_.location, file->content, errorCode);
            }
        }
        throw ParseError(msg, cur_.location);
    }
    next();
}


std::unique_ptr<ProgramAST> Parser::parseProgram()
{
    auto prog = std::make_unique<ProgramAST>();
    while (cur_.kind != tok_eof)
    {
        auto s = parseStatement();
        if (verbose_)
            printf("[parser] parsed statement\n");
        if (s)
            prog->stmts.push_back(std::move(s));
    }
    return prog;
}

std::unique_ptr<StmtAST> Parser::parseStatement()
{
    if (verbose_) {
        printf("[parser] parseStatement entry: kind=%d text='%s' at %d:%d\n", (int)cur_.kind, cur_.text.c_str(), cur_.location.line, cur_.location.column);
    }
    // break statement
    if (cur_.kind == tok_break)
    {
        next();
        if (cur_.kind == tok_semicolon) next();
        return std::make_unique<BreakStmt>();
    }

    // continue statement
    if (cur_.kind == tok_continue)
    {
        next();
        if (cur_.kind == tok_semicolon) next();
        return std::make_unique<ContinueStmt>();
    }

    if (cur_.kind == tok_include)
    {
        next();
        std::vector<std::string> includePaths;
        
        // Allow import { "file1.k", "file2.k" }
        if (cur_.kind == tok_brace_open) {
            next();
            // Expect at least one string or closing brace
            while (cur_.kind != tok_brace_close && cur_.kind != tok_eof) {
                if (cur_.kind != tok_string) {
                    throw ParseError("expected string literal in import list", cur_.location);
                }
                includePaths.push_back(cur_.text);
                next();
                if (cur_.kind == tok_comma) {
                    next();
                } else if (cur_.kind != tok_brace_close) {
                    throw ParseError("expected ',' or '}' in import list", cur_.location);
                }
            }
            if (cur_.kind != tok_brace_close) {
                throw ParseError("expected '}' to close import list", cur_.location);
            }
            next();
        } else {
            if (cur_.kind != tok_string) {
                auto* er = errorReporter();
                auto* sm = sourceManager();
                if (er && sm) {
                    auto file = sm->getFile(cur_.location.filename);
                    if (file) {
                        throw EnhancedParseError("expected string literal path after import", cur_.location, file->content, ErrorCodes::INVALID_SYNTAX);
                    }
                }
                throw ParseError("expected string literal path after import", cur_.location);
            }
            includePaths.push_back(cur_.text);
            next();
        }
        
        if (cur_.kind == tok_semicolon)
            next();

        auto inc = std::make_unique<IncludeStmt>();
        
        // Process each imported file
        for (const std::string& path : includePaths) {
            // Read file contents
            std::ifstream f(path);
            if (!f.is_open()) {
                if (verbose_) {
                    printf("[parser] warning: could not open import file: %s\n", path.c_str());
                }
                continue;
            }
            std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();

            // Add file to source manager for proper error reporting
            if (ctx_) {
                ctx_->sourceManager.addFile(path, contents);
            }
            
            // Track imported file
            inc->importedFiles.push_back(path);

            Lexer sublex(contents, verbose_, path);
            Parser subparser(sublex, verbose_, ctx_);
            auto subprog = subparser.parseProgram();

            for (auto &s : subprog->stmts)
            {
                inc->stmts.push_back(std::move(s));
            }
        }
        return inc;
    }

    // include("path.k"); - deprecated function-style syntax
    if (cur_.kind == tok_identifier && cur_.text == "include")
    {
        next();
        if (cur_.kind != tok_paren_open)
            throw ParseError("expected '(' after include (deprecated: use 'import \"file.k\"' instead)", cur_.location);
        next();
        if (cur_.kind != tok_string)
            throw ParseError("expected string literal path in include", cur_.location);
        std::string path = cur_.text;
        next();
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')' after include path", cur_.location);
        next();
        if (cur_.kind != tok_semicolon)
            throw ParseError("expected ';' after include", cur_.location);
        next();

        // Read file contents
        std::ifstream f(path);
        if (!f.is_open())
            throw ParseError(std::string("failed to open import file: ") + path, cur_.location);
        std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        // Add file to source manager for proper error reporting
        if (ctx_) {
            ctx_->sourceManager.addFile(path, contents);
        }

        Lexer sublex(contents, verbose_, path);
        Parser subparser(sublex, verbose_, ctx_);
        auto subprog = subparser.parseProgram();

        auto inc = std::make_unique<IncludeStmt>();
        inc->importedFiles.push_back(path);
        for (auto &s : subprog->stmts)
        {
            inc->stmts.push_back(std::move(s));
        }
        return inc;
    }

        if (cur_.kind == tok_struct)
    {
        return parseStructDef();
    }

        if (cur_.kind == tok_impl)
    {
        return parseImpl();
    }

        if (cur_.kind == tok_extern)
    {
        next();
        
        // Expect "C" string literal
        if (cur_.kind != tok_string || cur_.text != "C")
            throw ParseError("expected \"C\" after extern", cur_.location);
        next();
        
                if (cur_.kind == tok_brace_open) {
            next();
            auto inc = std::make_unique<IncludeStmt>();
            
                        while (cur_.kind != tok_brace_close && cur_.kind != tok_eof) {
                // Supported forms:
                                
                if (cur_.kind == tok_struct) {
                                        next(); // consume 'struct'
                    if (cur_.kind != tok_identifier)
                        throw ParseError("expected struct name after 'struct'", cur_.location);
                    std::string structName = cur_.text;
                    next();
                    if (cur_.kind != tok_semicolon)
                        throw ParseError("expected ';' after extern struct declaration", cur_.location);
                    next();
                    inc->stmts.push_back(std::make_unique<ExternStructDeclAST>(structName));
                }
                else {
                                        if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                        throw ParseError("expected return type or 'struct' in extern C block", cur_.location);
                    std::string returnType = parseTypeString();
                    if (cur_.kind != tok_identifier)
                        throw ParseError("expected function name in extern C block", cur_.location);
                    std::string funcName = cur_.text; next();
                    if (cur_.kind != tok_paren_open)
                        throw ParseError("expected '(' after function name", cur_.location);
                    next();
                    std::vector<std::pair<std::string, std::string>> params;
                    if (cur_.kind != tok_paren_close) {
                        while (true) {
                            // Variadic ...
                            if (cur_.kind == tok_range) { next(); if (cur_.kind == tok_dot) next(); params.emplace_back("...", "..."); break; }
                            if (cur_.kind == tok_dot) { int dots=0; while (cur_.kind==tok_dot && dots<3){ next(); dots++; } params.emplace_back("...","..."); break; }
                            if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                                throw ParseError("expected parameter type", cur_.location);
                            std::string pt = parseTypeString();
                            params.emplace_back("", pt);
                            if (cur_.kind == tok_comma) { next(); continue; }
                            break;
                        }
                    }
                    if (cur_.kind != tok_paren_close)
                        throw ParseError("expected ')' after parameters", cur_.location);
                    next();
                    if (cur_.kind != tok_semicolon)
                        throw ParseError("expected ';' after extern function declaration", cur_.location);
                    next();
                    inc->stmts.push_back(std::make_unique<ExternFunctionAST>(funcName, returnType, params));
                }
            }
            
            if (cur_.kind != tok_brace_close)
                throw ParseError("expected '}' to close extern C block", cur_.location);
            next();
            
            return inc;
        }
        
                                if (cur_.kind == tok_struct) {
            next();
            if (cur_.kind != tok_identifier)
                throw ParseError("expected struct name after 'struct'", cur_.location);
            std::string structName = cur_.text;
            next();
            if (cur_.kind != tok_semicolon)
                throw ParseError("expected ';' after extern struct declaration", cur_.location);
            next();
            return std::make_unique<ExternStructDeclAST>(structName);
        }
                // Parse return type
        if (cur_.kind != tok_identifier && cur_.kind != tok_bool &&
            cur_.kind != tok_int && cur_.kind != tok_str && 
            cur_.kind != tok_float && cur_.kind != tok_double)
            throw ParseError("expected return type after extern \"C\"", cur_.location);
        std::string returnType = parseTypeString();
        
        // Parse function name
        if (cur_.kind != tok_identifier)
            throw ParseError("expected function name", cur_.location);
        std::string funcName = cur_.text;
        next();
        
        // Parse parameter list
        if (cur_.kind != tok_paren_open)
            throw ParseError("expected '(' after function name", cur_.location);
        next();
        
        std::vector<std::pair<std::string, std::string>> params;
        if (cur_.kind != tok_paren_close)
        {
            while (true)
            {
                if (cur_.kind != tok_identifier)
                    throw ParseError("expected parameter name", cur_.location);
                std::string paramName = cur_.text;
                next();
                
                if (cur_.kind != tok_colon)
                    throw ParseError("expected ':' after parameter name", cur_.location);
                next();
                
                if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                    throw ParseError("expected parameter type after ':'", cur_.location);
                std::string paramType = parseTypeString();
                
                params.emplace_back(paramName, paramType);
                
                if (cur_.kind == tok_comma)
                {
                    next();
                    continue;
                }
                break;
            }
        }
        
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')' after extern function parameters", cur_.location);
        next();
        
        if (cur_.kind != tok_semicolon)
            throw ParseError("expected ';' after extern function declaration", cur_.location);
        next();
        
        return std::make_unique<ExternFunctionAST>(funcName, returnType, params);
    }

        if (cur_.kind == tok_identifier && cur_.text == "for")
    {
        next();
                if (cur_.kind != tok_paren_open)
            throw ParseError("expected '(' after 'for'", cur_.location);

        next(); // consume '('

                        std::unique_ptr<StmtAST> initStmt;
        if (cur_.kind != tok_semicolon && cur_.kind != tok_paren_close) {
            if (cur_.kind == tok_var) {
                SourceLocation forVarLoc = cur_.location; // Save location of 'var'
                next();
                if (cur_.kind != tok_identifier)
                    throw ParseError("expected variable name after 'var' in for(...)", cur_.location);
                std::string loopVar = cur_.text; next();
                if (cur_.kind == tok_in) {
                    // for (var i in expr)
                    next();
                    auto rangeExpr = parseExpression();
                    if (cur_.kind != tok_paren_close)
                        throw ParseError("expected ')' after range in for(...)", cur_.location);
                    next();
                    if (cur_.kind != tok_brace_open)
                        throw ParseError("expected '{' after for(...)", cur_.location);
                    next();
                    std::vector<std::unique_ptr<StmtAST>> body;
                    while (cur_.kind != tok_brace_close) {
                        if (cur_.kind == tok_eof)
                            throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
                        auto s = parseStatement();
                        if (s) body.push_back(std::move(s));
                    }
                    next();
                    return std::make_unique<ForStmt>(loopVar, std::move(rangeExpr), std::move(body));
                }
                                // var name[: type] = expr ;
                std::string varType = "auto";
                if (cur_.kind == tok_colon) {
                    next();
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                        throw ParseError("expected type after ':' in for-init", cur_.location);
                    varType = parseTypeString();
                }
                if (cur_.kind != tok_equal) throw ParseError("expected '=' in for-init declaration", cur_.location);
                next();
                auto initExpr = parseExpression();
                if (cur_.kind != tok_semicolon) throw ParseError("expected ';' after for-init", cur_.location);
                next();
                auto varDeclStmt = std::make_unique<VarDeclStmt>(varType, loopVar, std::move(initExpr));
                varDeclStmt->location = forVarLoc;
                initStmt = std::move(varDeclStmt);
            } else if (isTypeToken(cur_) || (cur_.kind == tok_identifier && peekToken().kind == tok_identifier)) {
                                SourceLocation forTypeLoc = cur_.location; // Save location of type
                std::string maybeType = parseTypeString();
                if (cur_.kind == tok_identifier) {
                    std::string loopVar = cur_.text; next();
                    if (cur_.kind == tok_in) {
                        // for (int i in expr)
                        next();
                        auto rangeExpr = parseExpression();
                        if (cur_.kind != tok_paren_close)
                            throw ParseError("expected ')' after range in for(...)", cur_.location);
                        next();
                        if (cur_.kind != tok_brace_open)
                            throw ParseError("expected '{' after for(...)", cur_.location);
                        next();
                        std::vector<std::unique_ptr<StmtAST>> body;
                        while (cur_.kind != tok_brace_close) {
                            if (cur_.kind == tok_eof)
                                throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
                            auto s = parseStatement();
                            if (s) body.push_back(std::move(s));
                        }
                        next();
                        return std::make_unique<ForStmt>(loopVar, std::move(rangeExpr), std::move(body));
                    }
                                        if (cur_.kind != tok_equal) throw ParseError("expected '=' in for-init declaration", cur_.location);
                    next();
                    auto initExpr = parseExpression();
                    if (cur_.kind != tok_semicolon) throw ParseError("expected ';' after for-init", cur_.location);
                    next();
                    auto varDeclStmt = std::make_unique<VarDeclStmt>(maybeType, loopVar, std::move(initExpr));
                    varDeclStmt->location = forTypeLoc;
                    initStmt = std::move(varDeclStmt);
                } else {
                    throw ParseError("expected variable name after type in for-init", cur_.location);
                }
            } else if (cur_.kind == tok_identifier) {
                                std::string loopVar = cur_.text; next();
                if (cur_.kind == tok_in) {
                    next();
                    auto rangeExpr = parseExpression();
                    if (cur_.kind != tok_paren_close)
                        throw ParseError("expected ')' after range in for(...)", cur_.location);
                    next();
                    if (cur_.kind != tok_brace_open)
                        throw ParseError("expected '{' after for(...)", cur_.location);
                    next();
                    std::vector<std::unique_ptr<StmtAST>> body;
                    while (cur_.kind != tok_brace_close) {
                        if (cur_.kind == tok_eof)
                            throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
                        auto s = parseStatement();
                        if (s) body.push_back(std::move(s));
                    }
                    next();
                    return std::make_unique<ForStmt>(loopVar, std::move(rangeExpr), std::move(body));
                }
                                if (cur_.kind != tok_equal) throw ParseError("expected '=' in for-init assignment", cur_.location);
                next();
                auto rhs = parseExpression();
                if (cur_.kind != tok_semicolon) throw ParseError("expected ';' after for-init", cur_.location);
                next();
                initStmt = std::make_unique<AssignStmtAST>(loopVar, std::move(rhs));
            }
        } else {
            // Empty init
            next();
        }

                std::unique_ptr<ExprAST> condExpr;
        if (cur_.kind == tok_semicolon) {
            next();
            condExpr = std::make_unique<BoolExprAST>(true);
        } else {
            condExpr = parseExpression();
            if (cur_.kind != tok_semicolon) throw ParseError("expected ';' after for condition", cur_.location);
            next();
        }

                std::unique_ptr<StmtAST> incrStmt;
        if (cur_.kind != tok_paren_close) {
            if (cur_.kind == tok_identifier) {
                std::string lhsName = cur_.text; next();
                if (cur_.kind != tok_equal) throw ParseError("expected '=' in for-increment", cur_.location);
                next();
                auto rhs = parseExpression();
                incrStmt = std::make_unique<AssignStmtAST>(lhsName, std::move(rhs));
            } else {
                                auto e = parseExpression();
                incrStmt = std::make_unique<ExprStmtAST>(std::move(e));
            }
        }
        if (cur_.kind != tok_paren_close) throw ParseError("expected ')' after for clauses", cur_.location);
        next();
        if (cur_.kind != tok_brace_open) throw ParseError("expected '{' after for(...)", cur_.location);
        next();

        std::vector<std::unique_ptr<StmtAST>> body;
        while (cur_.kind != tok_brace_close) {
            if (cur_.kind == tok_eof)
                throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
            auto s = parseStatement();
            if (s) body.push_back(std::move(s));
        }
        next(); // consume '}'

                if (incrStmt) body.push_back(std::move(incrStmt));

        // Build while(cond) { body }
        auto whileStmt = std::make_unique<WhileStmt>(std::move(condExpr), std::move(body));

                auto block = std::make_unique<IncludeStmt>();
        if (initStmt) block->stmts.push_back(std::move(initStmt));
        block->stmts.push_back(std::move(whileStmt));
        return block;
    }

            if (isTypeToken(cur_) || (cur_.kind == tok_identifier && cur_.text != "for" && peekToken().kind == tok_identifier))
    {
        SourceLocation typeDeclLoc = cur_.location; // Save location of type
        if (verbose_) printf("[parser] type-first branch: cur kind=%d text='%s' next kind=%d text='%s'\n",
            (int)cur_.kind, cur_.text.c_str(), (int)peekToken().kind, peekToken().text.c_str());
        std::string typeOrReturn = parseTypeString();
        if (cur_.kind != tok_identifier)
            throw ParseError("expected identifier after type", cur_.location);
        std::string name = cur_.text;
        if (verbose_) printf("[parser] after type '%s', name='%s' kind=%d next text='%s'\n",
            typeOrReturn.c_str(), name.c_str(), (int)cur_.kind, cur_.text.c_str());
        next();
        if (cur_.kind == tok_paren_open)
        {
                        next();
            std::vector<std::pair<std::string, std::string>> params;
            if (cur_.kind != tok_paren_close)
            {
                while (true)
                {
                    // Variadic: ...
                    if (cur_.kind == tok_range) {
                        next();
                        if (cur_.kind == tok_dot) next();
                        params.emplace_back("...", "...");
                        break;
                    }
                    if (cur_.kind == tok_dot) {
                        int dotCount = 0;
                        while (cur_.kind == tok_dot && dotCount < 3) { next(); dotCount++; }
                        params.emplace_back("...", "...");
                        break;
                    }
                    if (verbose_) printf("[parser] function param parse: kind=%d text='%s'\n", (int)cur_.kind, cur_.text.c_str());
                    if (cur_.kind != tok_identifier) {
                        throw ParseError("expected parameter name (func def)", cur_.location);
                    }
                    std::string pname = cur_.text; next();
                    if (cur_.kind != tok_colon) {
                        throw ParseError("expected ':' after parameter name", cur_.location);
                    }
                    next();
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier) {
                        throw ParseError("expected parameter type after ':'", cur_.location);
                    }
                    std::string ptype = parseTypeString();
                    params.emplace_back(pname, ptype);
                    if (cur_.kind == tok_comma) { next(); continue; }
                    break;
                }
            }
            if (cur_.kind != tok_paren_close)
                throw ParseError("expected ')' after parameters", cur_.location);
            next();
            if (cur_.kind != tok_brace_open)
                throw ParseError("expected '{' before function body", cur_.location);
            next();
            std::vector<std::unique_ptr<StmtAST>> body;
            while (cur_.kind != tok_brace_close)
            {
                if (cur_.kind == tok_eof)
                    throw ParseError("unexpected end of file in function body; expected '}'", cur_.location);
                auto s = parseStatement();
                if (s) body.push_back(std::move(s));
            }
            next(); // '}'
            return std::make_unique<FunctionAST>(name, typeOrReturn, params, std::move(body));
        }
        if (cur_.kind != tok_equal)
            throw ParseError("expected '=' after variable name", cur_.location);
        next();
        auto initExpr = parseExpression();
        SourceLocation semicolonLoc = cur_.location;
        if (cur_.kind != tok_semicolon)
        {
            auto* er = errorReporter();
            auto* sm = sourceManager();
            int span = 1;
            if (er && sm) {
                auto file = sm->getFile(semicolonLoc.filename);
                if (file) {
                    SourceLocation errorLoc = semicolonLoc;
                    if (errorLoc.line > 1 && errorLoc.column <= 5) {
                        errorLoc.line -= 1;
                        if (errorLoc.line - 1 < file->lines.size()) {
                            errorLoc.column = static_cast<int>(file->lines[errorLoc.line - 1].length());
                        }
                    }
                    throw EnhancedParseError("expected ';' after variable declaration", errorLoc, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                }
            }
            throw ParseError("expected ';' after variable declaration", semicolonLoc);
        }
        next();
        if (verbose_)
            printf("[parser] parsed typed variable declaration: %s %s\n", typeOrReturn.c_str(), name.c_str());
        auto stmt = std::make_unique<VarDeclStmt>(typeOrReturn, name, std::move(initExpr));
        stmt->location = typeDeclLoc; // Set the location of the VarDeclStmt
        return stmt;
    }

    // If/elif/else statement
    if (cur_.kind == tok_if) {
        if (verbose_) printf("[parser] parsing if statement\n");
        next();
        if (cur_.kind != tok_paren_open) throw ParseError("expected '(' after 'if'", cur_.location);
        next();
        if (verbose_) printf("[parser] parsing if condition\n");
        auto cond = parseExpression();
        if (verbose_) printf("[parser] parsed if condition\n");
        if (cur_.kind != tok_paren_close) throw ParseError("expected ')' after if condition", cur_.location);
        next();
        if (cur_.kind != tok_brace_open) throw ParseError("expected '{' after if condition", cur_.location);
        next();
        std::vector<std::unique_ptr<StmtAST>> thenBody;
        while (cur_.kind != tok_brace_close) {
            thenBody.push_back(parseStatement());
        }
        next(); // consume '}'
        std::vector<std::pair<std::unique_ptr<ExprAST>, std::vector<std::unique_ptr<StmtAST>>>> elifs;
        while (cur_.kind == tok_elif) {
            next();
            if (cur_.kind != tok_paren_open) throw ParseError("expected '(' after 'elif'", cur_.location);
            next();
            auto elifCond = parseExpression();
            if (cur_.kind != tok_paren_close) throw ParseError("expected ')' after elif condition", cur_.location);
            next();
            if (cur_.kind != tok_brace_open) throw ParseError("expected '{' after elif condition", cur_.location);
            next();
            std::vector<std::unique_ptr<StmtAST>> elifBody;
            while (cur_.kind != tok_brace_close) {
                elifBody.push_back(parseStatement());
            }
            next(); // consume '}'
            elifs.emplace_back(std::move(elifCond), std::move(elifBody));
        }
        std::vector<std::unique_ptr<StmtAST>> elseBody;
                while (cur_.kind == tok_else) {
            next();
            if (cur_.kind == tok_if) {
                                next();
                if (cur_.kind != tok_paren_open) throw ParseError("expected '(' after 'if' in 'else if'", cur_.location);
                next();
                auto elifCond = parseExpression();
                if (cur_.kind != tok_paren_close) throw ParseError("expected ')' after 'else if' condition", cur_.location);
                next();
                if (cur_.kind != tok_brace_open) throw ParseError("expected '{' after 'else if' condition", cur_.location);
                next();
                std::vector<std::unique_ptr<StmtAST>> elifBody;
                while (cur_.kind != tok_brace_close) {
                    elifBody.push_back(parseStatement());
                }
                next(); // consume '}'
                elifs.emplace_back(std::move(elifCond), std::move(elifBody));
                                continue;
            }
            // Plain else block
            if (cur_.kind != tok_brace_open) throw ParseError("expected '{' after 'else'", cur_.location);
            next();
            while (cur_.kind != tok_brace_close) {
                elseBody.push_back(parseStatement());
            }
            next(); // consume '}'
            break; // only one else block allowed
        }
        return std::make_unique<IfStmtAST>(std::move(cond), std::move(thenBody), std::move(elifs), std::move(elseBody));
    }

    
        if (cur_.kind == tok_var)
    {
        SourceLocation varDeclLoc = cur_.location; // Save location of 'var' keyword
        next();
        if (cur_.kind != tok_identifier)
            throw ParseError("expected variable name after 'var'", cur_.location);
        auto varName = cur_.text;
        SourceLocation varNameLoc = cur_.location; // Save location of variable name
        next();
        
        std::string varType = "auto"; // default to auto/inferred
        
                if (cur_.kind == tok_colon) {
            next();
            if (!isTypeToken(cur_) && cur_.kind != tok_identifier) {
                throw ParseError("expected type after ':'", cur_.location);
            }
            varType = parseTypeString();
        }
        
        if (cur_.kind != tok_equal)
            throw ParseError("expected '=' after variable name", cur_.location);
        next();
        auto initExpr = parseExpression();
        SourceLocation semicolonLoc = cur_.location;
        if (cur_.kind != tok_semicolon)
        {
            auto* er = errorReporter();
            auto* sm = sourceManager();
            int span = 1;
            if (er && sm) {
                auto file = sm->getFile(semicolonLoc.filename);
                if (file) {
                    SourceLocation errorLoc = semicolonLoc;
                    if (errorLoc.line > 1 && errorLoc.column <= 5) {
                        errorLoc.line -= 1;
                        if (errorLoc.line - 1 < file->lines.size()) {
                            errorLoc.column = static_cast<int>(file->lines[errorLoc.line - 1].length());
                        }
                    }
                    throw EnhancedParseError("expected ';' after variable declaration", errorLoc, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                }
            }
            throw ParseError("expected ';' after variable declaration", semicolonLoc);
        }
        next();
        if (verbose_)
            printf("[parser] parsed dynamic variable declaration: %s\n", varName.c_str());
        auto stmt = std::make_unique<VarDeclStmt>(varType, varName, std::move(initExpr));
        stmt->location = varDeclLoc; // Set the location of the VarDeclStmt
        return stmt;
    }

    
    // While-loop
    if (cur_.kind == tok_while)
    {
        next();
        if (cur_.kind != tok_paren_open)
            throw ParseError("expected '(' after 'while'", cur_.location);
        next();
        
        auto condition = parseExpression();
        
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')' after while condition", cur_.location);
        next();
        
        if (cur_.kind != tok_brace_open)
            throw ParseError("expected '{' after while condition", cur_.location);
        next();
        
        std::vector<std::unique_ptr<StmtAST>> body;
        while (cur_.kind != tok_brace_close)
        {
            if (cur_.kind == tok_eof)
                throw ParseError("unexpected end of file in while-loop body; expected '}'", cur_.location);
            auto s = parseStatement();
            if (s)
                body.push_back(std::move(s));
        }
        next();
        
        if (verbose_)
            printf("[parser] parsed while loop\n");
        
        return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
    }

    if (cur_.kind == tok_ret)
    {
        next();
        auto returnValue = parseExpression();
        
        if (cur_.kind != tok_semicolon)
        {
            auto* er = errorReporter();
            auto* sm = sourceManager();
            int span = std::max(1, (int)cur_.text.size());
            if (er && sm) {
                auto file = sm->getFile(cur_.location.filename);
                if (file) {
                    throw EnhancedParseError("expected ';' after return statement", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                }
            }
            throw ParseError("expected ';' after return statement", cur_.location);
        }
        next();
        
        return std::make_unique<ReturnStmt>(std::move(returnValue));
    }

    if (cur_.kind == tok_match)
    {
        next();
        // Parse match expression - but we need to stop before '{' since that starts the match body
        // For simple cases (variable name), just parse the identifier directly
        std::unique_ptr<ExprAST> matchExpr;
        if (cur_.kind == tok_identifier) {
            std::string varName = cur_.text;
            next();
            // Check if this is a simple variable (next token is '{')
            // or a more complex expression (has operators)
            if (cur_.kind == tok_brace_open) {
                // Simple variable case - don't try to parse as struct literal
                matchExpr = std::make_unique<VariableExprAST>(varName);
            } else if (cur_.kind == tok_dot || cur_.kind == tok_square_bracket_open) {
                // Member access or array access - need to continue parsing
                auto varExpr = std::make_unique<VariableExprAST>(varName);
                // Put it back for proper expression parsing
                // Actually, let's just parse the rest as a continuation
                if (cur_.kind == tok_dot) {
                    next();
                    if (cur_.kind != tok_identifier)
                        throw ParseError("expected field name after '.'", cur_.location);
                    std::string fieldName = cur_.text;
                    next();
                    matchExpr = std::make_unique<MemberAccessExpr>(std::move(varExpr), fieldName);
                } else if (cur_.kind == tok_square_bracket_open) {
                    next();
                    auto indexExpr = parseExpression();
                    if (cur_.kind != tok_square_bracket_close)
                        throw ParseError("expected ']' after array index", cur_.location);
                    next();
                    matchExpr = std::make_unique<ArrayAccessExpr>(std::move(varExpr), std::move(indexExpr));
                }
            } else {
                // Has operators - parse as full expression  
                // Need to reconstruct - this is tricky, let's just handle simple cases
                matchExpr = std::make_unique<VariableExprAST>(varName);
            }
        } else {
            // For non-identifier expressions, use regular parseExpression
            matchExpr = parseExpression();
        }
        
        if (cur_.kind != tok_brace_open)
            throw ParseError("expected '{' after match expression", cur_.location);
        next();
        
        auto matchStmt = std::make_unique<MatchStmt>(std::move(matchExpr));
        
        while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
        {
            MatchArm arm;
            
            // Check for wildcard pattern (_)
            if (cur_.kind == tok_identifier && cur_.text == "_") {
                arm.isWildcard = true;
                arm.pattern = nullptr;
                next();
            } else {
                arm.isWildcard = false;
                arm.pattern = parseExpression();
            }
            
            if (cur_.kind != tok_fat_arrow)
                throw ParseError("expected '=>' after match pattern", cur_.location);
            next();
            
            if (cur_.kind == tok_brace_open) {
                next();
                while (cur_.kind != tok_brace_close && cur_.kind != tok_eof) {
                    arm.body.push_back(parseStatement());
                }
                if (cur_.kind != tok_brace_close)
                    throw ParseError("expected '}' to close match arm body", cur_.location);
                next();
            } else {
                arm.body.push_back(parseStatement());
            }
            
            if (cur_.kind == tok_comma)
                next();
            
            matchStmt->arms.push_back(std::move(arm));
        }
        
        if (cur_.kind != tok_brace_close)
            throw ParseError("expected '}' to close match statement", cur_.location);
        next();
        
        return matchStmt;
    }

    if (cur_.kind == tok_mul)
    {
        next();
        auto ptrExpr = parseExpression();
        
        if (cur_.kind == tok_equal)
        {
            next();
            auto value = parseExpression();
            if (cur_.kind != tok_semicolon) {
                auto* er = errorReporter();
                auto* sm = sourceManager();
                int span = std::max(1, (int)cur_.text.size());
                if (er && sm) {
                    auto file = sm->getFile(cur_.location.filename);
                    if (file) {
                        throw EnhancedParseError("expected ';' after assignment", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                    }
                }
                throw ParseError("expected ';' after assignment", cur_.location);
            }
            next();
            
            auto derefExpr = std::make_unique<DereferenceExpr>(std::move(ptrExpr));
            return std::make_unique<DerefAssignStmt>(std::move(derefExpr), std::move(value));
        }
        else
        {
            throw ParseError("expected '=' after dereference in assignment", cur_.location);
        }
    }

    if (cur_.kind == tok_identifier || cur_.kind == tok_this)
    {
        auto expr = parseExpression();
        
        char compoundOp = 0;
        if (cur_.kind == tok_plus_eq) compoundOp = '+';
        else if (cur_.kind == tok_minus_eq) compoundOp = '-';
        else if (cur_.kind == tok_mul_eq) compoundOp = '*';
        else if (cur_.kind == tok_div_eq) compoundOp = '/';
        else if (cur_.kind == tok_mod_eq) compoundOp = '%';
        
        if (cur_.kind == tok_equal || compoundOp != 0)
        {
            next();
            auto rhs = parseExpression();
            
            std::unique_ptr<ExprAST> value;
            if (compoundOp != 0) {
                std::unique_ptr<ExprAST> lhsCopy;
                if (auto varExpr = dynamic_cast<VariableExprAST*>(expr.get())) {
                    lhsCopy = std::make_unique<VariableExprAST>(varExpr->name);
                } else if (auto memberAccess = dynamic_cast<MemberAccessExpr*>(expr.get())) {
                    auto objCopy = std::make_unique<VariableExprAST>(
                        dynamic_cast<VariableExprAST*>(memberAccess->object.get())->name);
                    lhsCopy = std::make_unique<MemberAccessExpr>(std::move(objCopy), memberAccess->fieldName);
                } else if (auto arrayAccess = dynamic_cast<ArrayAccessExpr*>(expr.get())) {
                    auto arrCopy = std::make_unique<VariableExprAST>(
                        dynamic_cast<VariableExprAST*>(arrayAccess->array.get())->name);
                    auto idxCopy = std::make_unique<NumberExprAST>(
                        dynamic_cast<NumberExprAST*>(arrayAccess->index.get())->value);
                    lhsCopy = std::make_unique<ArrayAccessExpr>(std::move(arrCopy), std::move(idxCopy));
                }
                value = std::make_unique<BinaryExprAST>(compoundOp, std::move(lhsCopy), std::move(rhs));
            } else {
                value = std::move(rhs);
            }
            
            if (cur_.kind != tok_semicolon) {
                auto* er = errorReporter();
                auto* sm = sourceManager();
                int span = std::max(1, (int)cur_.text.size());
                if (er && sm) {
                    auto file = sm->getFile(cur_.location.filename);
                    if (file) {
                        throw EnhancedParseError("expected ';' after assignment", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                    }
                }
                throw ParseError("expected ';' after assignment", cur_.location);
            }
            next();
            
            if (auto memberAccess = dynamic_cast<MemberAccessExpr*>(expr.get()))
            {
                auto object = std::move(memberAccess->object);
                std::string fieldName = memberAccess->fieldName;
                expr.release();
                return std::make_unique<MemberAssignStmt>(std::move(object), fieldName, std::move(value));
            }
            else if (auto arrayAccess = dynamic_cast<ArrayAccessExpr*>(expr.get()))
            {
                auto array = std::move(arrayAccess->array);
                auto index = std::move(arrayAccess->index);
                expr.release();
                return std::make_unique<ArrayAssignStmt>(std::move(array), std::move(index), std::move(value));
            }
            else if (auto varExpr = dynamic_cast<VariableExprAST*>(expr.get()))
            {
                std::string varName = varExpr->name;
                expr.release();
                return std::make_unique<AssignStmtAST>(varName, std::move(value));
            }
            else
            {
                throw ParseError("invalid assignment target", cur_.location);
            }
        }
        
        if (cur_.kind != tok_semicolon) {
            auto* er = errorReporter();
            auto* sm = sourceManager();
            int span = std::max(1, (int)cur_.text.size());
            if (er && sm) {
                auto file = sm->getFile(cur_.location.filename);
                if (file) {
                    throw EnhancedParseError("expected ';' after expression", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                }
            }
            throw ParseError("expected ';' after expression", cur_.location);
        }
        next();
        return std::make_unique<ExprStmtAST>(std::move(expr));
    }
    
    if (cur_.kind == tok_number || cur_.kind == tok_string || cur_.kind == tok_paren_open || cur_.kind == tok_this)
    {
        auto expr = parseExpression();
        if (cur_.kind != tok_semicolon) {
            auto* er = errorReporter();
            auto* sm = sourceManager();
            int span = std::max(1, (int)cur_.text.size());
            if (er && sm) {
                auto file = sm->getFile(cur_.location.filename);
                if (file) {
                    throw EnhancedParseError("expected ';' after expression", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                }
            }
            throw ParseError("expected ';' after expression", cur_.location);
        }
        next();
        return std::make_unique<ExprStmtAST>(std::move(expr));
    }

    {
        auto* er = errorReporter();
        auto* sm = sourceManager();
        if (er && sm) {
            auto file = sm->getFile(cur_.location.filename);
            if (file) {
                std::string errorMsg = "unknown statement";
                if (cur_.kind == tok_identifier) {
                    errorMsg = "unexpected identifier '" + cur_.text + "' - did you forget a type or keyword?";
                } else if (cur_.kind != tok_eof) {
                    errorMsg = "unexpected token '" + cur_.text + "' when expecting a statement";
                }
                throw EnhancedParseError(errorMsg, cur_.location, file->content, ErrorCodes::INVALID_SYNTAX);
            }
        }
    }
    throw ParseError("unknown statement", cur_.location);
}

std::unique_ptr<ExprAST> Parser::parseExpression(int precedence)
{
    auto lhs = parsePrimary();

    while (true)
    {
        int tokPrec = getTokPrecedence();
        if (tokPrec < precedence)
            break;

        char op = '?';
        if (cur_.kind == tok_and) {
            op = '&';
            if (verbose_) printf("[parser] recognized && operator\n");
        }
        else if (cur_.kind == tok_or) {
            op = '|';
            if (verbose_) printf("[parser] recognized || operator\n");
        }
        else if (cur_.kind == tok_eq) {
            op = '=';
            if (verbose_) printf("[parser] recognized == operator\n");
        }
        else if (cur_.kind == tok_ne) {
            op = 'n';
            if (verbose_) printf("[parser] recognized != operator\n");
        }
        else if (cur_.kind == tok_lt) {
            op = '<';
            if (verbose_) printf("[parser] recognized < operator\n");
        }
        else if (cur_.kind == tok_gt) {
            op = '>';
            if (verbose_) printf("[parser] recognized > operator\n");
        }
        else if (cur_.kind == tok_le) {
            op = 'l';
            if (verbose_) printf("[parser] recognized <= operator\n");
        }
        else if (cur_.kind == tok_ge) {
            op = 'g';
            if (verbose_) printf("[parser] recognized >= operator\n");
        }
        else if (cur_.kind == tok_plus) op = '+';
        else if (cur_.kind == tok_minus) op = '-';
        else if (cur_.kind == tok_mul) op = '*';
        else if (cur_.kind == tok_div) op = '/';
        else if (cur_.kind == tok_mod) op = '%';
        else if (cur_.kind == tok_dot) {
                        next(); // consume '.'
            if (cur_.kind != tok_identifier)
                throw ParseError("expected field name after '.'", cur_.location);
            std::string fieldName = cur_.text;
            next();
            
                        if (cur_.kind == tok_paren_open) {
                next(); // consume '('
                std::vector<std::unique_ptr<ExprAST>> args;
                
                while (cur_.kind != tok_paren_close) {
                    args.push_back(parseExpression());
                    if (cur_.kind == tok_comma) {
                        next();
                    } else if (cur_.kind != tok_paren_close) {
                        throw ParseError("expected ',' or ')' in method call arguments", cur_.location);
                    }
                }
                next(); // consume ')'
                
                // Check if lhs is a simple identifier (potential static call)
                // But NOT for "this" - that should always be a method call
                if (auto* varExpr = dynamic_cast<VariableExprAST*>(lhs.get())) {
                    if (varExpr->name == "this") {
                        lhs = std::make_unique<MethodCallExpr>(std::move(lhs), fieldName, std::move(args));
                    } else {
                        auto staticCall = std::make_unique<StaticCallExpr>(varExpr->name, fieldName);
                        staticCall->args = std::move(args);
                        staticCall->location = varExpr->location;
                        lhs = std::move(staticCall);
                    }
                } else {
                    // It's an instance method call on a complex expression
                    lhs = std::make_unique<MethodCallExpr>(std::move(lhs), fieldName, std::move(args));
                }
            } else {
                // Regular field access
                lhs = std::make_unique<MemberAccessExpr>(std::move(lhs), fieldName);
            }
            continue;         }
        else if (cur_.kind == tok_square_bracket_open) {
                        next(); // consume '['
            auto indexExpr = parseExpression();
            if (cur_.kind != tok_square_bracket_close)
                throw ParseError("expected ']' after array index", cur_.location);
            next(); // consume ']'
            lhs = std::make_unique<ArrayAccessExpr>(std::move(lhs), std::move(indexExpr));
            continue;         }
        else if (cur_.kind == tok_range) {
                        next(); // consume '..'
            auto end = parseExpression(tokPrec + 1);
            lhs = std::make_unique<RangeExpr>(std::move(lhs), std::move(end));
            continue;         }
        else {
            op = cur_.text.empty() ? '?' : cur_.text[0];
            if (verbose_) printf("[parser] fallback operator: '%c' (token kind: %d, text: '%s')\n", op, (int)cur_.kind, cur_.text.c_str());
        }
        if (verbose_) printf("[parser] using operator: '%c'\n", op);
        next();

        auto rhs = parseExpression(tokPrec + 1);
        lhs = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

std::unique_ptr<ExprAST> Parser::parsePrimary()
{
    // Handle unary operators
    if (cur_.kind == tok_minus)
    {
        char op = '-';
        next();
                        auto operand = parseExpression(39);
        return std::make_unique<UnaryExprAST>(op, std::move(operand));
    }
    if (cur_.kind == tok_not)
    {
        char op = '!';
        next();
                auto operand = parseExpression(39);
        return std::make_unique<UnaryExprAST>(op, std::move(operand));
    }
    
        if (cur_.kind == tok_ampersand)
    {
        next();
        auto operand = parsePrimary();
        return std::make_unique<AddressOfExpr>(std::move(operand));
    }
    
        if (cur_.kind == tok_mul)
    {
        next();
        auto operand = parsePrimary();
        return std::make_unique<DereferenceExpr>(std::move(operand));
    }

    if (cur_.kind == tok_number)
    {
        auto n = std::make_unique<NumberExprAST>(cur_.numberValue);
        next();
        return n;
    }
    if (cur_.kind == tok_string)
    {
        auto s = std::make_unique<StringExprAST>(cur_.text);
        next();
        return s;
    }
    if (cur_.kind == tok_paren_open)
    {
                SourceLocation openLoc = cur_.location;
        next(); // consume '('
                                bool isCast = isTypeToken(cur_);
        if (isCast) {
                        std::string typeName = parseTypeString();
            if (cur_.kind != tok_paren_close) {
                                                                throw ParseError("expected ')' after type in C-style cast", cur_.location);
            }
            next(); // consume ')'
                        auto operand = parsePrimary();
            return std::make_unique<CastExpr>(typeName, std::move(operand));
        }
                auto e = parseExpression();
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')'", cur_.location);
        next();
        return e;
    }

    // Array literal: [1, 2, 3, 4] or Map literal: ["key": "value", ...]
    if (cur_.kind == tok_square_bracket_open)
    {
        next(); // consume '['
        
        // Check if empty
        if (cur_.kind == tok_square_bracket_close) {
            next();
            // Empty array literal
            if (verbose_)
                printf("[parser] parsed empty array literal\n");
            return std::make_unique<ArrayLiteralExpr>(std::vector<std::unique_ptr<ExprAST>>());
        }
        
        // Parse first element to determine if it's array or map
        auto firstExpr = parseExpression();
        
        // Check if this is a map literal (has colon after first expression)
        if (cur_.kind == tok_colon) {
            // Map literal
            next(); // consume ':'
            auto firstValue = parseExpression();
            
            std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<ExprAST>>> pairs;
            pairs.emplace_back(std::move(firstExpr), std::move(firstValue));
            
            while (cur_.kind == tok_comma) {
                next(); // consume ','
                if (cur_.kind == tok_square_bracket_close) break; // trailing comma
                
                auto keyExpr = parseExpression();
                if (cur_.kind != tok_colon) {
                    throw ParseError("expected ':' after map key", cur_.location);
                }
                next(); // consume ':'
                auto valueExpr = parseExpression();
                pairs.emplace_back(std::move(keyExpr), std::move(valueExpr));
            }
            
            if (cur_.kind != tok_square_bracket_close) {
                throw ParseError("expected ']' to close map literal", cur_.location);
            }
            next();
            if (verbose_)
                printf("[parser] parsed map literal with %zu pairs\n", pairs.size());
            return std::make_unique<MapLiteralExpr>(std::move(pairs));
        } else {
            // Array literal
            std::vector<std::unique_ptr<ExprAST>> elements;
            elements.push_back(std::move(firstExpr));
            
            while (cur_.kind == tok_comma)
            {
                next();
                if (cur_.kind == tok_square_bracket_close) break; // trailing comma
                elements.push_back(parseExpression());
            }
            
            if (cur_.kind != tok_square_bracket_close)
            {
                throw ParseError("expected ']' to close array literal", cur_.location);
            }
            next();
            if (verbose_)
                printf("[parser] parsed array literal with %zu elements\n", elements.size());
            return std::make_unique<ArrayLiteralExpr>(std::move(elements));
        }
    }
        if (cur_.kind == tok_brace_open)
    {
        throw ParseError("unexpected '{' in expression; use '[' and ']' for array literals or 'Type { ... }' for struct literals", cur_.location);
    }

    if (cur_.kind == tok_identifier)
    {
        std::string idName = cur_.text;
        SourceLocation idLoc = cur_.location;
        next();

        // Struct literal
        if (cur_.kind == tok_brace_open)
        {
            return parseStructLiteral(idName);
        }

        // Function call
        if (cur_.text == "(")
        {
            next();
            std::vector<std::unique_ptr<ExprAST>> args;
            if (cur_.text != ")")
            {
                while (true)
                {
                    args.push_back(parseExpression());
                    if (cur_.text == ")")
                        break;
                    if (cur_.text != ",")
                        throw ParseError("expected ',' or ')' in call", cur_.location);
                    next();
                }
            }
            next();
            auto call = std::make_unique<CallExprAST>(idName, std::move(args));
            call->location = idLoc;
            return call;
        }

        auto varExpr = std::make_unique<VariableExprAST>(idName);
        varExpr->location = idLoc;
        return varExpr;
    }

    if (cur_.kind == tok_true) {
        next();
        return std::make_unique<BoolExprAST>(true);
    }
    if (cur_.kind == tok_false) {
        next();
        return std::make_unique<BoolExprAST>(false);
    }

        if (cur_.kind == tok_this) {
        next();
        return std::make_unique<VariableExprAST>("this");
    }

    if (cur_.kind == tok_null) {
        next();
        return std::make_unique<NullExprAST>();
    }

    throw ParseError("unknown token when expecting an expression", cur_.location);
}

int Parser::getTokPrecedence()
{
        if (cur_.kind == tok_or)
        return 5;
    if (cur_.kind == tok_and)
        return 10;
            if (cur_.kind == tok_range)
        return 12;
    if (cur_.kind == tok_eq || cur_.kind == tok_ne)
        return 15;
    if (cur_.kind == tok_lt || cur_.kind == tok_gt || cur_.kind == tok_le || cur_.kind == tok_ge)
        return 17;
    if (cur_.kind == tok_plus || cur_.kind == tok_minus)
        return 20;
    if (cur_.kind == tok_mul || cur_.kind == tok_div || cur_.kind == tok_mod)
        return 30;
    if (cur_.kind == tok_dot)
        return 40;     if (cur_.kind == tok_square_bracket_open)
        return 50;     
        if (cur_.kind == tok_semicolon || cur_.kind == tok_paren_close || 
        cur_.kind == tok_comma || cur_.kind == tok_brace_close ||
        cur_.kind == tok_square_bracket_close ||
        cur_.kind == tok_colon ||
        cur_.kind == tok_fat_arrow ||
        cur_.kind == tok_eof)
        return -1;
        
    return -1;
}

std::unique_ptr<StructDefStmt> Parser::parseStructDef()
{
    next(); // consume 'struct'
    
    if (cur_.kind != tok_identifier)
        throw ParseError("expected struct name after 'struct'", cur_.location);
    
    std::string structName = cur_.text;
    next();
    
        std::string parentName = "";
    if (cur_.kind == tok_colon) {
        next();
        if (cur_.kind != tok_identifier) {
            throw ParseError("expected parent struct name after ':'", cur_.location);
        }
        parentName = cur_.text;
        next();
    }
    
    if (cur_.kind != tok_brace_open)
        throw ParseError("expected '{' after struct name", cur_.location);
    next();
    
    std::vector<std::pair<std::string, std::string>> fields;
    std::vector<std::unique_ptr<FunctionAST>> methods;
    
    while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
    {
                if (cur_.kind == tok_data) {
            next(); // consume 'data'
            if (cur_.kind != tok_brace_open) {
                throw ParseError("expected '{' after 'data'", cur_.location);
            }
            next();
            
                        while (cur_.kind != tok_brace_close && cur_.kind != tok_eof) {
                if (cur_.kind != tok_identifier) {
                    throw ParseError("expected field name in data block", cur_.location);
                }
                
                std::string fieldName = cur_.text;
                next();
                
                if (cur_.kind != tok_colon) {
                    throw ParseError("expected ':' after field name", cur_.location);
                }
                next();
                
                if (!isTypeToken(cur_) && !(cur_.kind == tok_identifier)) {
                    throw ParseError("expected field type after ':'", cur_.location);
                }

                std::string fieldType = parseTypeString();
                
                fields.emplace_back(fieldName, fieldType);
                
                                if (cur_.kind == tok_comma || cur_.kind == tok_semicolon) {
                    next();
                } else if (cur_.kind != tok_brace_close) {
                    throw ParseError("expected ',' or ';' or '}' after field declaration", cur_.location);
                }
            }
            
            if (cur_.kind != tok_brace_close) {
                throw ParseError("expected '}' to close data block", cur_.location);
            }
            next(); // consume '}'
        }
                else if ((cur_.kind == tok_extend) || (cur_.kind == tok_impl) || isTypeToken(cur_) || cur_.kind == tok_identifier) {
                        if (cur_.kind == tok_extend || cur_.kind == tok_impl) {
                next();
            }
            // Return type
            if (!isTypeToken(cur_) && cur_.kind != tok_identifier) {
                throw ParseError("expected return type for method", cur_.location);
            }
            std::string returnType = parseTypeString();
            // Method name
            if (cur_.kind != tok_identifier) {
                throw ParseError("expected method name after return type", cur_.location);
            }
            auto methodName = cur_.text; next();
            // Parameters
            if (cur_.kind != tok_paren_open) {
                throw ParseError("expected '(' after method name", cur_.location);
            }
            next();
            std::vector<std::pair<std::string, std::string>> params;
            if (cur_.kind != tok_paren_close) {
                while (true) {
                    if (cur_.kind != tok_identifier) {
                        throw ParseError("expected parameter name", cur_.location);
                    }
                    std::string paramName = cur_.text; next();
                    if (cur_.kind != tok_colon) {
                        throw ParseError("expected ':' after parameter name", cur_.location);
                    }
                    next();
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier) {
                        throw ParseError("expected parameter type after ':'", cur_.location);
                    }
                    std::string paramType = parseTypeString();
                    params.emplace_back(paramName, paramType);
                    if (cur_.kind == tok_comma) { next(); continue; }
                    break;
                }
            }
            if (cur_.kind != tok_paren_close) {
                throw ParseError("expected ')' after parameters", cur_.location);
            }
            next();
            if (cur_.kind != tok_brace_open) {
                throw ParseError("expected '{' to start method body", cur_.location);
            }
            next();
            // Body
            std::vector<std::unique_ptr<StmtAST>> body;
            while (cur_.kind != tok_brace_close) {
                body.push_back(parseStatement());
            }
            next(); // consume '}'
            methods.push_back(std::make_unique<FunctionAST>(methodName, returnType, params, std::move(body)));
        }
        else {
            throw ParseError("expected 'data' block or method definition in struct", cur_.location);
        }
    }
    
    if (cur_.kind != tok_brace_close)
        throw ParseError("expected '}' to close struct definition", cur_.location);
    next();
    
    if (verbose_)
        printf("[parser] parsed struct definition: %s with %zu fields and %zu methods\n", 
               structName.c_str(), fields.size(), methods.size());
    
    auto structDef = std::make_unique<StructDefStmt>(structName, std::move(fields));
    structDef->parentName = parentName;
    structDef->methods = std::move(methods);
    return structDef;
}

std::unique_ptr<StructLiteralExpr> Parser::parseStructLiteral(const std::string &structName)
{
    if (cur_.kind != tok_brace_open)
        throw ParseError("expected '{' for struct literal", cur_.location);
    next();
    
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> fieldValues;
    
    while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
    {
        if (cur_.kind != tok_identifier)
            throw ParseError("expected field name in struct literal", cur_.location);
        
        std::string fieldName = cur_.text;
        next();
        
        if (cur_.kind != tok_colon)
            throw ParseError("expected ':' after field name in struct literal", cur_.location);
        next();
        
        auto fieldValue = parseExpression();
        fieldValues.emplace_back(fieldName, std::move(fieldValue));
        
        if (cur_.kind == tok_comma)
        {
            next();
        }
        else if (cur_.kind != tok_brace_close)
        {
            throw ParseError("expected ',' or '}' after field value in struct literal", cur_.location);
        }
    }
    
    if (cur_.kind != tok_brace_close)
        throw ParseError("expected '}' to close struct literal", cur_.location);
    next();
    
    if (verbose_)
        printf("[parser] parsed struct literal: %s with %zu field values\n", structName.c_str(), fieldValues.size());
    
    return std::make_unique<StructLiteralExpr>(structName, std::move(fieldValues));
}

std::unique_ptr<ImplStmt> Parser::parseImpl()
{
    next(); // consume 'impl'
    
    if (cur_.kind != tok_identifier)
        throw ParseError("expected struct name after 'impl'", cur_.location);
    
    std::string structName = cur_.text;
    next();
    
    if (cur_.kind != tok_brace_open)
        throw ParseError("expected '{' after impl struct name", cur_.location);
    next();
    
    auto impl = std::make_unique<ImplStmt>(structName);
    
    while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
    {
                if (isTypeToken(cur_) || cur_.kind == tok_identifier)
        {
            std::string returnType = parseTypeString();
            if (cur_.kind != tok_identifier)
                throw ParseError("expected method name after return type", cur_.location);
            std::string methodName = cur_.text; next();
            if (cur_.kind != tok_paren_open)
                throw ParseError("expected '(' after method name", cur_.location);
            next();
            std::vector<std::pair<std::string, std::string>> params;
            while (cur_.kind != tok_paren_close && cur_.kind != tok_eof)
            {
                if (cur_.kind != tok_identifier)
                    throw ParseError("expected parameter name", cur_.location);
                std::string paramName = cur_.text; next();
                if (cur_.kind != tok_colon)
                    throw ParseError("expected ':' after parameter name", cur_.location);
                next();
                if (!isTypeToken(cur_) && !(cur_.kind == tok_identifier))
                    throw ParseError("expected parameter type", cur_.location);
                std::string paramType = parseTypeString();
                params.emplace_back(paramName, paramType);
                if (cur_.kind == tok_comma) { next(); continue; }
                break;
            }
            if (cur_.kind != tok_paren_close)
                throw ParseError("expected ')' after parameters", cur_.location);
            next();
            if (cur_.kind != tok_brace_open)
                throw ParseError("expected '{' to start method body", cur_.location);
            next();
            std::vector<std::unique_ptr<StmtAST>> body;
            while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
            {
                body.push_back(parseStatement());
            }
            if (cur_.kind != tok_brace_close)
                throw ParseError("expected '}' to close method body", cur_.location);
            next();
                        std::string mangledName = structName + "::" + methodName;
            auto method = std::make_unique<FunctionAST>(mangledName, returnType, params, std::move(body));
            impl->methods.push_back(std::move(method));
        }
        else
        {
            throw ParseError("expected method definition in impl block", cur_.location);
        }
    }
    
    if (cur_.kind != tok_brace_close)
        throw ParseError("expected '}' to close impl block", cur_.location);
    next();
    
    if (verbose_)
        printf("[parser] parsed impl block for %s with %zu methods\n", structName.c_str(), impl->methods.size());
    
    return impl;
}

std::unique_ptr<StaticCallExpr> Parser::parseStaticCall(const std::string &structName)
{
    next(); // consume '.'
    
    if (cur_.kind != tok_identifier)
        throw ParseError("expected method name after '.'", cur_.location);
    
    std::string methodName = cur_.text;
    next();
    
    if (cur_.kind != tok_paren_open)
        throw ParseError("expected '(' after static method name", cur_.location);
    next();
    
    auto staticCall = std::make_unique<StaticCallExpr>(structName, methodName);
    
    // Parse arguments
    while (cur_.kind != tok_paren_close && cur_.kind != tok_eof)
    {
        staticCall->args.push_back(parseExpression());
        
        if (cur_.kind == tok_comma)
        {
            next();
        }
        else if (cur_.kind != tok_paren_close)
        {
            throw ParseError("expected ',' or ')' in static method call", cur_.location);
        }
    }
    
    if (cur_.kind != tok_paren_close)
        throw ParseError("expected ')' to close static method call", cur_.location);
    next();
    
    if (verbose_)
        printf("[parser] parsed static call: %s.%s with %zu args\n", structName.c_str(), methodName.c_str(), staticCall->args.size());
    
    return staticCall;
}
