#include "../include/parser.h"
#include "../include/compilation_context.h"
#include "../include/error_reporter.h"
#include "../include/source_manager.h"
#include "../include/module_resolver.h"
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

Parser::Parser(Lexer &lex, bool verbose, CompilationContext *ctx)
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
    if (hasPeeked_)
    {
        cur_ = peeked_;
        hasPeeked_ = false;
    }
    else
    {
        cur_ = lex_.next();
    }
}

Token Parser::peekToken()
{
    if (!hasPeeked_)
    {
        peeked_ = lex_.next();
        hasPeeked_ = true;
    }
    return peeked_;
}

ErrorReporter *Parser::errorReporter() const
{
    if (ctx_)
        return &ctx_->errorReporter;
    return g_errorReporter.get();
}

SourceManager *Parser::sourceManager() const
{
    if (ctx_)
        return &ctx_->sourceManager;
    return g_sourceManager.get();
}

void Parser::expect(TokenKind kind, const std::string &msg)
{
    if (cur_.kind != kind)
    {
        auto *er = errorReporter();
        auto *sm = sourceManager();
        if (er && sm)
        {
            auto file = sm->getFile(cur_.location.filename);
            if (file)
            {
                std::string errorCode = ErrorCodes::UNEXPECTED_TOKEN;
                if (msg.find("semicolon") != std::string::npos || msg.find("';'") != std::string::npos)
                {
                    errorCode = ErrorCodes::MISSING_SEMICOLON;
                }
                else if (msg.find("brace") != std::string::npos || msg.find("'{'") != std::string::npos || msg.find("'}'") != std::string::npos)
                {
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
    if (verbose_)
    {
        printf("[parser] parseStatement entry: kind=%d text='%s' at %d:%d\n", (int)cur_.kind, cur_.text.c_str(), cur_.location.line, cur_.location.column);
    }

    if (cur_.kind == tok_module)
    {
        SourceLocation moduleLoc = cur_.location;
        next();

        if (cur_.kind != tok_identifier)
        {
            auto *er = errorReporter();
            auto *sm = sourceManager();
            if (er && sm)
            {
                auto file = sm->getFile(cur_.location.filename);
                if (file)
                {
                    throw EnhancedParseError("expected module name after 'module'", cur_.location, file->content, ErrorCodes::INVALID_SYNTAX);
                }
            }
            throw ParseError("expected module name after 'module'", cur_.location);
        }

        std::string moduleName = cur_.text;
        next();

        if (cur_.kind == tok_semicolon)
            next();

        auto modDecl = std::make_unique<ModuleDeclStmt>(moduleName);
        modDecl->location = moduleLoc;
        return modDecl;
    }

    if (cur_.kind == tok_break)
    {
        next();
        if (cur_.kind == tok_semicolon)
            next();
        return std::make_unique<BreakStmt>();
    }

    if (cur_.kind == tok_continue)
    {
        next();
        if (cur_.kind == tok_semicolon)
            next();
        return std::make_unique<ContinueStmt>();
    }

    if (cur_.kind == tok_include)
    {
        SourceLocation importLoc = cur_.location;
        next();
        std::vector<std::string> modulePaths;

        if (cur_.kind == tok_brace_open)
        {
            next();
            while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
            {
                if (cur_.kind == tok_identifier)
                {

                    std::string moduleName = cur_.text;
                    next();
                    while (cur_.kind == tok_div)
                    {
                        next();
                        if (cur_.kind != tok_identifier)
                        {
                            throw ParseError("expected module name after '/'", cur_.location);
                        }
                        moduleName += "/" + cur_.text;
                        next();
                    }
                    modulePaths.push_back(moduleName);
                }
                else
                {
                    throw ParseError("expected module name in import list", cur_.location);
                }

                if (cur_.kind == tok_comma)
                {
                    next();
                }
                else if (cur_.kind != tok_brace_close)
                {
                    throw ParseError("expected ',' or '}' in import list", cur_.location);
                }
            }
            if (cur_.kind != tok_brace_close)
            {
                throw ParseError("expected '}' to close import list", cur_.location);
            }
            next();
        }
        else if (cur_.kind == tok_identifier)
        {

            std::string moduleName = cur_.text;
            next();
            while (cur_.kind == tok_div)
            {
                next();
                if (cur_.kind != tok_identifier)
                {
                    throw ParseError("expected module name after '/'", cur_.location);
                }
                moduleName += "/" + cur_.text;
                next();
            }
            modulePaths.push_back(moduleName);
        }
        else
        {
            auto *er = errorReporter();
            auto *sm = sourceManager();
            if (er && sm)
            {
                auto file = sm->getFile(cur_.location.filename);
                if (file)
                {
                    throw EnhancedParseError("expected module name after 'import'", cur_.location, file->content, ErrorCodes::INVALID_SYNTAX);
                }
            }
            throw ParseError("expected module name after 'import'", cur_.location);
        }

        if (cur_.kind == tok_semicolon)
            next();

        auto inc = std::make_unique<IncludeStmt>();
        inc->location = importLoc;

        for (const std::string &modulePath : modulePaths)
        {
            std::string resolvedPath;

            if (g_moduleResolver)
            {
                auto resolved = g_moduleResolver->resolve(modulePath, cur_.location.filename);
                if (resolved)
                {
                    resolvedPath = resolved->string();
                }
                else
                {
                    throw ParseError("could not resolve module '" + modulePath + "'", importLoc);
                }
            }
            else
            {

                resolvedPath = "lib/" + modulePath + "/" + modulePath + ".k";
                std::string baseMod = modulePath;
                auto slashPos = modulePath.find('/');
                if (slashPos != std::string::npos)
                {
                    baseMod = modulePath.substr(0, slashPos);
                    resolvedPath = "lib/" + baseMod + "/" + modulePath.substr(slashPos + 1) + ".k";
                }
            }

            std::ifstream f(resolvedPath);
            if (!f.is_open())
            {
                if (verbose_)
                {
                    printf("[parser] warning: could not open module file: %s\n", resolvedPath.c_str());
                }
                throw ParseError("could not open module '" + modulePath + "' at " + resolvedPath, importLoc);
            }
            std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();

            if (ctx_)
            {
                ctx_->sourceManager.addFile(resolvedPath, contents);
            }

            inc->importedFiles.push_back(resolvedPath);

            Lexer sublex(contents, verbose_, resolvedPath);
            Parser subparser(sublex, verbose_, ctx_);
            auto subprog = subparser.parseProgram();

            for (auto &s : subprog->stmts)
            {
                inc->stmts.push_back(std::move(s));
            }
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

        if (cur_.kind != tok_string || cur_.text != "C")
            throw ParseError("expected \"C\" after extern", cur_.location);
        next();

        if (cur_.kind == tok_brace_open)
        {
            next();
            auto inc = std::make_unique<IncludeStmt>();

            while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
            {

                if (cur_.kind == tok_struct)
                {
                    next();
                    if (cur_.kind != tok_identifier)
                        throw ParseError("expected struct name after 'struct'", cur_.location);
                    std::string structName = cur_.text;
                    next();
                    if (cur_.kind != tok_semicolon)
                        throw ParseError("expected ';' after extern struct declaration", cur_.location);
                    next();
                    inc->stmts.push_back(std::make_unique<ExternStructDeclAST>(structName));
                }
                else
                {
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                        throw ParseError("expected return type or 'struct' in extern C block", cur_.location);
                    std::string typeName = parseTypeString();
                    if (cur_.kind != tok_identifier)
                        throw ParseError("expected function or variable name in extern C block", cur_.location);
                    std::string name = cur_.text;
                    next();

                    if (cur_.kind == tok_semicolon)
                    {

                        next();
                        inc->stmts.push_back(std::make_unique<ExternVarAST>(name, typeName));
                    }
                    else if (cur_.kind == tok_paren_open)
                    {

                        next();
                        std::vector<std::pair<std::string, std::string>> params;
                        if (cur_.kind != tok_paren_close)
                        {
                            while (true)
                            {

                                if (cur_.kind == tok_range)
                                {
                                    next();
                                    if (cur_.kind == tok_dot)
                                        next();
                                    params.emplace_back("...", "...");
                                    break;
                                }
                                if (cur_.kind == tok_dot)
                                {
                                    int dots = 0;
                                    while (cur_.kind == tok_dot && dots < 3)
                                    {
                                        next();
                                        dots++;
                                    }
                                    params.emplace_back("...", "...");
                                    break;
                                }

                                if (cur_.kind == tok_identifier)
                                {

                                    std::string first = cur_.text;
                                    next();
                                    if (cur_.kind == tok_colon)
                                    {

                                        next();
                                        if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                                            throw ParseError("expected parameter type after ':'", cur_.location);
                                        std::string pt = parseTypeString();
                                        params.emplace_back(first, pt);
                                    }
                                    else
                                    {

                                        std::string paramTypeName = first;
                                        while (cur_.kind == tok_mul)
                                        {
                                            paramTypeName += "*";
                                            next();
                                        }
                                        params.emplace_back("", paramTypeName);
                                    }
                                }
                                else if (isTypeToken(cur_))
                                {

                                    std::string pt = parseTypeString();
                                    params.emplace_back("", pt);
                                }
                                else
                                {
                                    throw ParseError("expected parameter name or type", cur_.location);
                                }
                                if (cur_.kind == tok_comma)
                                {
                                    next();
                                    continue;
                                }
                                break;
                            }
                        }
                        if (cur_.kind != tok_paren_close)
                            throw ParseError("expected ')' after parameters", cur_.location);
                        next();
                        if (cur_.kind != tok_semicolon)
                            throw ParseError("expected ';' after extern function declaration", cur_.location);
                        next();
                        inc->stmts.push_back(std::make_unique<ExternFunctionAST>(name, typeName, params));
                    }
                    else
                    {
                        throw ParseError("expected '(' for function or ';' for variable declaration", cur_.location);
                    }
                }
            }

            if (cur_.kind != tok_brace_close)
                throw ParseError("expected '}' to close extern C block", cur_.location);
            next();

            return inc;
        }

        if (cur_.kind == tok_struct)
        {
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

        if (cur_.kind != tok_identifier && cur_.kind != tok_bool &&
            cur_.kind != tok_int && cur_.kind != tok_str &&
            cur_.kind != tok_float && cur_.kind != tok_double)
            throw ParseError("expected return type after extern \"C\"", cur_.location);
        std::string returnType = parseTypeString();

        if (cur_.kind != tok_identifier)
            throw ParseError("expected function name", cur_.location);
        std::string funcName = cur_.text;
        next();

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

        next();

        std::unique_ptr<StmtAST> initStmt;
        if (cur_.kind != tok_semicolon && cur_.kind != tok_paren_close)
        {
            if (cur_.kind == tok_var)
            {
                SourceLocation forVarLoc = cur_.location;
                next();
                if (cur_.kind != tok_identifier)
                    throw ParseError("expected variable name after 'var' in for(...)", cur_.location);
                std::string loopVar = cur_.text;
                next();
                if (cur_.kind == tok_in)
                {

                    next();
                    auto rangeExpr = parseExpression();
                    if (cur_.kind != tok_paren_close)
                        throw ParseError("expected ')' after range in for(...)", cur_.location);
                    next();
                    if (cur_.kind != tok_brace_open)
                        throw ParseError("expected '{' after for(...)", cur_.location);
                    next();
                    std::vector<std::unique_ptr<StmtAST>> body;
                    while (cur_.kind != tok_brace_close)
                    {
                        if (cur_.kind == tok_eof)
                            throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
                        auto s = parseStatement();
                        if (s)
                            body.push_back(std::move(s));
                    }
                    next();
                    return std::make_unique<ForStmt>(loopVar, std::move(rangeExpr), std::move(body));
                }

                std::string varType = "auto";
                if (cur_.kind == tok_colon)
                {
                    next();
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                        throw ParseError("expected type after ':' in for-init", cur_.location);
                    varType = parseTypeString();
                }
                if (cur_.kind != tok_equal)
                    throw ParseError("expected '=' in for-init declaration", cur_.location);
                next();
                auto initExpr = parseExpression();
                if (cur_.kind != tok_semicolon)
                    throw ParseError("expected ';' after for-init", cur_.location);
                next();
                auto varDeclStmt = std::make_unique<VarDeclStmt>(varType, loopVar, std::move(initExpr));
                varDeclStmt->location = forVarLoc;
                initStmt = std::move(varDeclStmt);
            }
            else if (isTypeToken(cur_) || (cur_.kind == tok_identifier && peekToken().kind == tok_identifier))
            {
                SourceLocation forTypeLoc = cur_.location;
                std::string maybeType = parseTypeString();
                if (cur_.kind == tok_identifier)
                {
                    std::string loopVar = cur_.text;
                    next();
                    if (cur_.kind == tok_in)
                    {

                        next();
                        auto rangeExpr = parseExpression();
                        if (cur_.kind != tok_paren_close)
                            throw ParseError("expected ')' after range in for(...)", cur_.location);
                        next();
                        if (cur_.kind != tok_brace_open)
                            throw ParseError("expected '{' after for(...)", cur_.location);
                        next();
                        std::vector<std::unique_ptr<StmtAST>> body;
                        while (cur_.kind != tok_brace_close)
                        {
                            if (cur_.kind == tok_eof)
                                throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
                            auto s = parseStatement();
                            if (s)
                                body.push_back(std::move(s));
                        }
                        next();
                        return std::make_unique<ForStmt>(loopVar, std::move(rangeExpr), std::move(body));
                    }
                    if (cur_.kind != tok_equal)
                        throw ParseError("expected '=' in for-init declaration", cur_.location);
                    next();
                    auto initExpr = parseExpression();
                    if (cur_.kind != tok_semicolon)
                        throw ParseError("expected ';' after for-init", cur_.location);
                    next();
                    auto varDeclStmt = std::make_unique<VarDeclStmt>(maybeType, loopVar, std::move(initExpr));
                    varDeclStmt->location = forTypeLoc;
                    initStmt = std::move(varDeclStmt);
                }
                else
                {
                    throw ParseError("expected variable name after type in for-init", cur_.location);
                }
            }
            else if (cur_.kind == tok_identifier)
            {
                std::string loopVar = cur_.text;
                next();
                if (cur_.kind == tok_in)
                {
                    next();
                    auto rangeExpr = parseExpression();
                    if (cur_.kind != tok_paren_close)
                        throw ParseError("expected ')' after range in for(...)", cur_.location);
                    next();
                    if (cur_.kind != tok_brace_open)
                        throw ParseError("expected '{' after for(...)", cur_.location);
                    next();
                    std::vector<std::unique_ptr<StmtAST>> body;
                    while (cur_.kind != tok_brace_close)
                    {
                        if (cur_.kind == tok_eof)
                            throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
                        auto s = parseStatement();
                        if (s)
                            body.push_back(std::move(s));
                    }
                    next();
                    return std::make_unique<ForStmt>(loopVar, std::move(rangeExpr), std::move(body));
                }
                if (cur_.kind != tok_equal)
                    throw ParseError("expected '=' in for-init assignment", cur_.location);
                next();
                auto rhs = parseExpression();
                if (cur_.kind != tok_semicolon)
                    throw ParseError("expected ';' after for-init", cur_.location);
                next();
                initStmt = std::make_unique<AssignStmtAST>(loopVar, std::move(rhs));
            }
        }
        else
        {

            next();
        }

        std::unique_ptr<ExprAST> condExpr;
        if (cur_.kind == tok_semicolon)
        {
            next();
            condExpr = std::make_unique<BoolExprAST>(true);
        }
        else
        {
            condExpr = parseExpression();
            if (cur_.kind != tok_semicolon)
                throw ParseError("expected ';' after for condition", cur_.location);
            next();
        }

        std::unique_ptr<StmtAST> incrStmt;
        if (cur_.kind != tok_paren_close)
        {
            if (cur_.kind == tok_identifier)
            {
                std::string lhsName = cur_.text;
                next();
                if (cur_.kind != tok_equal)
                    throw ParseError("expected '=' in for-increment", cur_.location);
                next();
                auto rhs = parseExpression();
                incrStmt = std::make_unique<AssignStmtAST>(lhsName, std::move(rhs));
            }
            else
            {
                auto e = parseExpression();
                incrStmt = std::make_unique<ExprStmtAST>(std::move(e));
            }
        }
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')' after for clauses", cur_.location);
        next();
        if (cur_.kind != tok_brace_open)
            throw ParseError("expected '{' after for(...)", cur_.location);
        next();

        std::vector<std::unique_ptr<StmtAST>> body;
        while (cur_.kind != tok_brace_close)
        {
            if (cur_.kind == tok_eof)
                throw ParseError("unexpected end of file in for-loop body; expected '}'", cur_.location);
            auto s = parseStatement();
            if (s)
                body.push_back(std::move(s));
        }
        next();

        if (incrStmt)
            body.push_back(std::move(incrStmt));

        auto whileStmt = std::make_unique<WhileStmt>(std::move(condExpr), std::move(body));

        auto block = std::make_unique<IncludeStmt>();
        if (initStmt)
            block->stmts.push_back(std::move(initStmt));
        block->stmts.push_back(std::move(whileStmt));
        return block;
    }

    if (cur_.kind == tok_map)
    {
        SourceLocation mapDeclLoc = cur_.location;
        next();

        if (cur_.kind != tok_identifier)
        {
            throw ParseError("expected variable name after 'map'", cur_.location);
        }
        std::string varName = cur_.text;
        next();

        std::unique_ptr<ExprAST> initExpr;
        if (cur_.kind == tok_equal)
        {
            next();
            initExpr = parseExpression();
        }
        else
        {

            initExpr = std::make_unique<MapLiteralExpr>(
                std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<ExprAST>>>());
        }

        if (cur_.kind == tok_semicolon)
        {
            next();
        }

        if (verbose_)
            printf("[parser] parsed map declaration: %s\n", varName.c_str());
        auto stmt = std::make_unique<VarDeclStmt>("map", varName, std::move(initExpr));
        stmt->location = mapDeclLoc;
        return stmt;
    }

    if (cur_.kind == tok_list)
    {
        SourceLocation listDeclLoc = cur_.location;
        next();

        if (cur_.kind != tok_identifier)
        {
            throw ParseError("expected variable name after 'list'", cur_.location);
        }
        std::string varName = cur_.text;
        next();

        std::unique_ptr<ExprAST> initExpr;
        if (cur_.kind == tok_equal)
        {
            next();
            initExpr = parseExpression();
        }
        else
        {

            initExpr = std::make_unique<ListLiteralExpr>(std::vector<std::unique_ptr<ExprAST>>());
        }

        if (cur_.kind == tok_semicolon)
        {
            next();
        }

        if (verbose_)
            printf("[parser] parsed list declaration: %s\n", varName.c_str());
        auto stmt = std::make_unique<VarDeclStmt>("list", varName, std::move(initExpr));
        stmt->location = listDeclLoc;
        return stmt;
    }

    if (isTypeToken(cur_) || (cur_.kind == tok_identifier && cur_.text != "for" && peekToken().kind == tok_identifier))
    {
        SourceLocation typeDeclLoc = cur_.location;
        if (verbose_)
            printf("[parser] type-first branch: cur kind=%d text='%s' next kind=%d text='%s'\n",
                   (int)cur_.kind, cur_.text.c_str(), (int)peekToken().kind, peekToken().text.c_str());
        std::string typeOrReturn = parseTypeString();
        if (cur_.kind != tok_identifier)
            throw ParseError("expected identifier after type", cur_.location);
        std::string name = cur_.text;
        if (verbose_)
            printf("[parser] after type '%s', name='%s' kind=%d next text='%s'\n",
                   typeOrReturn.c_str(), name.c_str(), (int)cur_.kind, cur_.text.c_str());
        next();
        if (cur_.kind == tok_paren_open)
        {
            next();
            std::vector<std::pair<std::string, std::string>> params;
            bool isExtension = false;
            std::string extensionType;
            if (cur_.kind != tok_paren_close)
            {
                while (true)
                {

                    if (cur_.kind == tok_range)
                    {
                        next();
                        if (cur_.kind == tok_dot)
                            next();
                        params.emplace_back("...", "...");
                        break;
                    }
                    if (cur_.kind == tok_dot)
                    {
                        int dotCount = 0;
                        while (cur_.kind == tok_dot && dotCount < 3)
                        {
                            next();
                            dotCount++;
                        }
                        params.emplace_back("...", "...");
                        break;
                    }
                    if (cur_.kind == tok_this)
                    {
                        next();
                        if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                        {
                            throw ParseError("expected type after 'this' in extension method parameter", cur_.location);
                        }
                        std::string ptype = parseTypeString();
                        if (cur_.kind != tok_identifier)
                        {
                            throw ParseError("expected parameter name after type in extension method", cur_.location);
                        }
                        std::string pname = cur_.text;
                        next();
                        isExtension = true;
                        extensionType = ptype;
                        params.emplace_back(pname, ptype);
                        if (cur_.kind == tok_comma)
                        {
                            next();
                            continue;
                        }
                        break;
                    }
                    if (verbose_)
                        printf("[parser] function param parse: kind=%d text='%s'\n", (int)cur_.kind, cur_.text.c_str());
                    if (cur_.kind != tok_identifier)
                    {
                        throw ParseError("expected parameter name (func def)", cur_.location);
                    }
                    std::string pname = cur_.text;
                    next();
                    if (cur_.kind != tok_colon)
                    {
                        throw ParseError("expected ':' after parameter name", cur_.location);
                    }
                    next();
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                    {
                        throw ParseError("expected parameter type after ':'", cur_.location);
                    }
                    std::string ptype = parseTypeString();
                    params.emplace_back(pname, ptype);
                    if (cur_.kind == tok_comma)
                    {
                        next();
                        continue;
                    }
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
                if (s)
                    body.push_back(std::move(s));
            }
            next();
            auto func = std::make_unique<FunctionAST>(name, typeOrReturn, params, std::move(body));
            func->isExtension = isExtension;
            func->extensionType = extensionType;
            return func;
        }
        if (cur_.kind != tok_equal)
            throw ParseError("expected '=' after variable name", cur_.location);
        next();
        auto initExpr = parseExpression();
        SourceLocation semicolonLoc = cur_.location;
        if (cur_.kind != tok_semicolon)
        {
            auto *er = errorReporter();
            auto *sm = sourceManager();
            int span = 1;
            if (er && sm)
            {
                auto file = sm->getFile(semicolonLoc.filename);
                if (file)
                {
                    SourceLocation errorLoc = semicolonLoc;
                    if (errorLoc.line > 1 && errorLoc.column <= 5)
                    {
                        errorLoc.line -= 1;
                        if (errorLoc.line - 1 < file->lines.size())
                        {
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
        stmt->location = typeDeclLoc;
        return stmt;
    }

    if (cur_.kind == tok_if)
    {
        if (verbose_)
            printf("[parser] parsing if statement\n");
        next();
        if (cur_.kind != tok_paren_open)
            throw ParseError("expected '(' after 'if'", cur_.location);
        next();
        if (verbose_)
            printf("[parser] parsing if condition\n");
        auto cond = parseExpression();
        if (verbose_)
            printf("[parser] parsed if condition\n");
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')' after if condition", cur_.location);
        next();
        if (cur_.kind != tok_brace_open)
            throw ParseError("expected '{' after if condition", cur_.location);
        next();
        std::vector<std::unique_ptr<StmtAST>> thenBody;
        while (cur_.kind != tok_brace_close)
        {
            thenBody.push_back(parseStatement());
        }
        next();
        std::vector<std::pair<std::unique_ptr<ExprAST>, std::vector<std::unique_ptr<StmtAST>>>> elifs;
        while (cur_.kind == tok_elif)
        {
            next();
            if (cur_.kind != tok_paren_open)
                throw ParseError("expected '(' after 'elif'", cur_.location);
            next();
            auto elifCond = parseExpression();
            if (cur_.kind != tok_paren_close)
                throw ParseError("expected ')' after elif condition", cur_.location);
            next();
            if (cur_.kind != tok_brace_open)
                throw ParseError("expected '{' after elif condition", cur_.location);
            next();
            std::vector<std::unique_ptr<StmtAST>> elifBody;
            while (cur_.kind != tok_brace_close)
            {
                elifBody.push_back(parseStatement());
            }
            next();
            elifs.emplace_back(std::move(elifCond), std::move(elifBody));
        }
        std::vector<std::unique_ptr<StmtAST>> elseBody;
        while (cur_.kind == tok_else)
        {
            next();
            if (cur_.kind == tok_if)
            {
                next();
                if (cur_.kind != tok_paren_open)
                    throw ParseError("expected '(' after 'if' in 'else if'", cur_.location);
                next();
                auto elifCond = parseExpression();
                if (cur_.kind != tok_paren_close)
                    throw ParseError("expected ')' after 'else if' condition", cur_.location);
                next();
                if (cur_.kind != tok_brace_open)
                    throw ParseError("expected '{' after 'else if' condition", cur_.location);
                next();
                std::vector<std::unique_ptr<StmtAST>> elifBody;
                while (cur_.kind != tok_brace_close)
                {
                    elifBody.push_back(parseStatement());
                }
                next();
                elifs.emplace_back(std::move(elifCond), std::move(elifBody));
                continue;
            }

            if (cur_.kind != tok_brace_open)
                throw ParseError("expected '{' after 'else'", cur_.location);
            next();
            while (cur_.kind != tok_brace_close)
            {
                elseBody.push_back(parseStatement());
            }
            next();
            break;
        }
        return std::make_unique<IfStmtAST>(std::move(cond), std::move(thenBody), std::move(elifs), std::move(elseBody));
    }

    if (cur_.kind == tok_var)
    {
        SourceLocation varDeclLoc = cur_.location;
        next();
        if (cur_.kind != tok_identifier)
            throw ParseError("expected variable name after 'var'", cur_.location);
        auto varName = cur_.text;
        SourceLocation varNameLoc = cur_.location;
        next();

        std::string varType = "auto";

        if (cur_.kind == tok_colon)
        {
            next();
            if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
            {
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
            auto *er = errorReporter();
            auto *sm = sourceManager();
            int span = 1;
            if (er && sm)
            {
                auto file = sm->getFile(semicolonLoc.filename);
                if (file)
                {
                    SourceLocation errorLoc = semicolonLoc;
                    if (errorLoc.line > 1 && errorLoc.column <= 5)
                    {
                        errorLoc.line -= 1;
                        if (errorLoc.line - 1 < file->lines.size())
                        {
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
        stmt->location = varDeclLoc;
        return stmt;
    }

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

        std::unique_ptr<ExprAST> returnValue = nullptr;
        if (cur_.kind != tok_semicolon)
        {
            returnValue = parseExpression();
        }

        if (cur_.kind != tok_semicolon)
        {
            auto *er = errorReporter();
            auto *sm = sourceManager();
            int span = std::max(1, (int)cur_.text.size());
            if (er && sm)
            {
                auto file = sm->getFile(cur_.location.filename);
                if (file)
                {
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

        std::unique_ptr<ExprAST> matchExpr;
        if (cur_.kind == tok_identifier)
        {
            std::string varName = cur_.text;
            next();

            if (cur_.kind == tok_brace_open)
            {

                matchExpr = std::make_unique<VariableExprAST>(varName);
            }
            else if (cur_.kind == tok_dot || cur_.kind == tok_square_bracket_open)
            {

                auto varExpr = std::make_unique<VariableExprAST>(varName);

                if (cur_.kind == tok_dot)
                {
                    next();
                    if (cur_.kind != tok_identifier)
                        throw ParseError("expected field name after '.'", cur_.location);
                    std::string fieldName = cur_.text;
                    next();
                    matchExpr = std::make_unique<MemberAccessExpr>(std::move(varExpr), fieldName);
                }
                else if (cur_.kind == tok_square_bracket_open)
                {
                    next();
                    auto indexExpr = parseExpression();
                    if (cur_.kind != tok_square_bracket_close)
                        throw ParseError("expected ']' after array index", cur_.location);
                    next();
                    matchExpr = std::make_unique<ArrayAccessExpr>(std::move(varExpr), std::move(indexExpr));
                }
            }
            else
            {

                matchExpr = std::make_unique<VariableExprAST>(varName);
            }
        }
        else
        {

            matchExpr = parseExpression();
        }

        if (cur_.kind != tok_brace_open)
            throw ParseError("expected '{' after match expression", cur_.location);
        next();

        auto matchStmt = std::make_unique<MatchStmt>(std::move(matchExpr));

        while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
        {
            MatchArm arm;

            if (cur_.kind == tok_identifier && cur_.text == "_")
            {
                arm.isWildcard = true;
                arm.pattern = nullptr;
                next();
            }
            else
            {
                arm.isWildcard = false;
                arm.pattern = parseExpression();
            }

            if (cur_.kind != tok_fat_arrow)
                throw ParseError("expected '=>' after match pattern", cur_.location);
            next();

            if (cur_.kind == tok_brace_open)
            {
                next();
                while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
                {
                    arm.body.push_back(parseStatement());
                }
                if (cur_.kind != tok_brace_close)
                    throw ParseError("expected '}' to close match arm body", cur_.location);
                next();
            }
            else
            {
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
            if (cur_.kind != tok_semicolon)
            {
                auto *er = errorReporter();
                auto *sm = sourceManager();
                int span = std::max(1, (int)cur_.text.size());
                if (er && sm)
                {
                    auto file = sm->getFile(cur_.location.filename);
                    if (file)
                    {
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
        if (cur_.kind == tok_plus_eq)
            compoundOp = '+';
        else if (cur_.kind == tok_minus_eq)
            compoundOp = '-';
        else if (cur_.kind == tok_mul_eq)
            compoundOp = '*';
        else if (cur_.kind == tok_div_eq)
            compoundOp = '/';
        else if (cur_.kind == tok_mod_eq)
            compoundOp = '%';

        if (cur_.kind == tok_equal || compoundOp != 0)
        {
            next();
            auto rhs = parseExpression();

            std::unique_ptr<ExprAST> value;
            if (compoundOp != 0)
            {
                std::unique_ptr<ExprAST> lhsCopy;
                if (auto varExpr = dynamic_cast<VariableExprAST *>(expr.get()))
                {
                    lhsCopy = std::make_unique<VariableExprAST>(varExpr->name);
                }
                else if (auto memberAccess = dynamic_cast<MemberAccessExpr *>(expr.get()))
                {
                    auto objCopy = std::make_unique<VariableExprAST>(
                        dynamic_cast<VariableExprAST *>(memberAccess->object.get())->name);
                    lhsCopy = std::make_unique<MemberAccessExpr>(std::move(objCopy), memberAccess->fieldName);
                }
                else if (auto arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr.get()))
                {
                    auto arrCopy = std::make_unique<VariableExprAST>(
                        dynamic_cast<VariableExprAST *>(arrayAccess->array.get())->name);
                    auto idxCopy = std::make_unique<NumberExprAST>(
                        dynamic_cast<NumberExprAST *>(arrayAccess->index.get())->value);
                    lhsCopy = std::make_unique<ArrayAccessExpr>(std::move(arrCopy), std::move(idxCopy));
                }
                value = std::make_unique<BinaryExprAST>(compoundOp, std::move(lhsCopy), std::move(rhs));
            }
            else
            {
                value = std::move(rhs);
            }

            if (cur_.kind != tok_semicolon)
            {
                auto *er = errorReporter();
                auto *sm = sourceManager();
                int span = std::max(1, (int)cur_.text.size());
                if (er && sm)
                {
                    auto file = sm->getFile(cur_.location.filename);
                    if (file)
                    {
                        throw EnhancedParseError("expected ';' after assignment", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                    }
                }
                throw ParseError("expected ';' after assignment", cur_.location);
            }
            next();

            if (auto memberAccess = dynamic_cast<MemberAccessExpr *>(expr.get()))
            {
                auto object = std::move(memberAccess->object);
                std::string fieldName = memberAccess->fieldName;
                expr.release();
                return std::make_unique<MemberAssignStmt>(std::move(object), fieldName, std::move(value));
            }
            else if (auto arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr.get()))
            {
                auto array = std::move(arrayAccess->array);
                auto index = std::move(arrayAccess->index);
                expr.release();
                return std::make_unique<ArrayAssignStmt>(std::move(array), std::move(index), std::move(value));
            }
            else if (auto varExpr = dynamic_cast<VariableExprAST *>(expr.get()))
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

        if (cur_.kind != tok_semicolon)
        {
            auto *er = errorReporter();
            auto *sm = sourceManager();
            int span = std::max(1, (int)cur_.text.size());
            if (er && sm)
            {
                auto file = sm->getFile(cur_.location.filename);
                if (file)
                {
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
        if (cur_.kind != tok_semicolon)
        {
            auto *er = errorReporter();
            auto *sm = sourceManager();
            int span = std::max(1, (int)cur_.text.size());
            if (er && sm)
            {
                auto file = sm->getFile(cur_.location.filename);
                if (file)
                {
                    throw EnhancedParseError("expected ';' after expression", cur_.location, file->content, ErrorCodes::MISSING_SEMICOLON, span);
                }
            }
            throw ParseError("expected ';' after expression", cur_.location);
        }
        next();
        return std::make_unique<ExprStmtAST>(std::move(expr));
    }

    {
        auto *er = errorReporter();
        auto *sm = sourceManager();
        if (er && sm)
        {
            auto file = sm->getFile(cur_.location.filename);
            if (file)
            {
                std::string errorMsg = "unknown statement";
                if (cur_.kind == tok_identifier)
                {
                    errorMsg = "unexpected identifier '" + cur_.text + "' - did you forget a type or keyword?";
                }
                else if (cur_.kind != tok_eof)
                {
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
        if (cur_.kind == tok_and)
        {
            op = '&';
            if (verbose_)
                printf("[parser] recognized && operator\n");
        }
        else if (cur_.kind == tok_or)
        {
            op = '|';
            if (verbose_)
                printf("[parser] recognized || operator\n");
        }
        else if (cur_.kind == tok_eq)
        {
            op = '=';
            if (verbose_)
                printf("[parser] recognized == operator\n");
        }
        else if (cur_.kind == tok_ne)
        {
            op = 'n';
            if (verbose_)
                printf("[parser] recognized != operator\n");
        }
        else if (cur_.kind == tok_lt)
        {
            op = '<';
            if (verbose_)
                printf("[parser] recognized < operator\n");
        }
        else if (cur_.kind == tok_gt)
        {
            op = '>';
            if (verbose_)
                printf("[parser] recognized > operator\n");
        }
        else if (cur_.kind == tok_le)
        {
            op = 'l';
            if (verbose_)
                printf("[parser] recognized <= operator\n");
        }
        else if (cur_.kind == tok_ge)
        {
            op = 'g';
            if (verbose_)
                printf("[parser] recognized >= operator\n");
        }
        else if (cur_.kind == tok_plus)
            op = '+';
        else if (cur_.kind == tok_minus)
            op = '-';
        else if (cur_.kind == tok_mul)
            op = '*';
        else if (cur_.kind == tok_div)
            op = '/';
        else if (cur_.kind == tok_mod)
            op = '%';

        else if (cur_.kind == tok_ampersand)
        {
            op = 'A';
            if (verbose_)
                printf("[parser] recognized & (bitwise AND) operator\n");
        }
        else if (cur_.kind == tok_bitwise_or)
        {
            op = 'O';
            if (verbose_)
                printf("[parser] recognized | (bitwise OR) operator\n");
        }
        else if (cur_.kind == tok_bitwise_xor)
        {
            op = 'X';
            if (verbose_)
                printf("[parser] recognized ^ (bitwise XOR) operator\n");
        }
        else if (cur_.kind == tok_shift_left)
        {
            op = 'L';
            if (verbose_)
                printf("[parser] recognized << (shift left) operator\n");
        }
        else if (cur_.kind == tok_shift_right)
        {
            op = 'R';
            if (verbose_)
                printf("[parser] recognized >> (shift right) operator\n");
        }
        else if (cur_.kind == tok_dot)
        {
            next();
            if (cur_.kind != tok_identifier)
                throw ParseError("expected field name after '.'", cur_.location);
            std::string fieldName = cur_.text;
            next();

            if (cur_.kind == tok_paren_open)
            {
                next();
                std::vector<std::unique_ptr<ExprAST>> args;

                while (cur_.kind != tok_paren_close)
                {
                    args.push_back(parseExpression());
                    if (cur_.kind == tok_comma)
                    {
                        next();
                    }
                    else if (cur_.kind != tok_paren_close)
                    {
                        throw ParseError("expected ',' or ')' in method call arguments", cur_.location);
                    }
                }
                next();

                if (auto *varExpr = dynamic_cast<VariableExprAST *>(lhs.get()))
                {
                    if (varExpr->name == "this")
                    {
                        lhs = std::make_unique<MethodCallExpr>(std::move(lhs), fieldName, std::move(args));
                    }
                    else
                    {
                        auto staticCall = std::make_unique<StaticCallExpr>(varExpr->name, fieldName);
                        staticCall->args = std::move(args);
                        staticCall->location = varExpr->location;
                        lhs = std::move(staticCall);
                    }
                }
                else
                {

                    lhs = std::make_unique<MethodCallExpr>(std::move(lhs), fieldName, std::move(args));
                }
            }
            else
            {

                lhs = std::make_unique<MemberAccessExpr>(std::move(lhs), fieldName);
            }
            continue;
        }
        else if (cur_.kind == tok_square_bracket_open)
        {
            next();
            auto indexExpr = parseExpression();
            if (cur_.kind != tok_square_bracket_close)
                throw ParseError("expected ']' after array index", cur_.location);
            next();
            lhs = std::make_unique<ArrayAccessExpr>(std::move(lhs), std::move(indexExpr));
            continue;
        }
        else if (cur_.kind == tok_range)
        {
            next();
            auto end = parseExpression(tokPrec + 1);
            lhs = std::make_unique<RangeExpr>(std::move(lhs), std::move(end));
            continue;
        }
        else
        {
            op = cur_.text.empty() ? '?' : cur_.text[0];
            if (verbose_)
                printf("[parser] fallback operator: '%c' (token kind: %d, text: '%s')\n", op, (int)cur_.kind, cur_.text.c_str());
        }
        if (verbose_)
            printf("[parser] using operator: '%c'\n", op);
        next();

        auto rhs = parseExpression(tokPrec + 1);
        lhs = std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs));
    }

    return lhs;
}

std::unique_ptr<ExprAST> Parser::parsePrimary()
{

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

    if (cur_.kind == tok_bitwise_not)
    {
        char op = '~';
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

    if (cur_.kind == tok_map)
    {
        SourceLocation mapLoc = cur_.location;
        next();

        if (cur_.kind != tok_brace_open)
        {
            throw ParseError("expected '{' after 'map' keyword", cur_.location);
        }
        next();

        std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<ExprAST>>> pairs;

        if (cur_.kind != tok_brace_close)
        {
            while (true)
            {
                auto keyExpr = parseExpression();
                if (cur_.kind != tok_colon)
                {
                    throw ParseError("expected ':' after map key", cur_.location);
                }
                next();
                auto valueExpr = parseExpression();
                pairs.emplace_back(std::move(keyExpr), std::move(valueExpr));

                if (cur_.kind == tok_comma)
                {
                    next();
                    if (cur_.kind == tok_brace_close)
                        break;
                    continue;
                }
                break;
            }
        }

        if (cur_.kind != tok_brace_close)
        {
            throw ParseError("expected '}' to close map literal", cur_.location);
        }
        next();

        if (verbose_)
            printf("[parser] parsed map literal with %zu pairs\n", pairs.size());
        auto expr = std::make_unique<MapLiteralExpr>(std::move(pairs));
        expr->location = mapLoc;
        return expr;
    }

    if (cur_.kind == tok_list)
    {
        SourceLocation listLoc = cur_.location;
        next();

        if (cur_.kind != tok_square_bracket_open)
        {
            throw ParseError("expected '[' after 'list' keyword", cur_.location);
        }
        next();

        std::vector<std::unique_ptr<ExprAST>> elements;

        if (cur_.kind != tok_square_bracket_close)
        {
            while (true)
            {
                elements.push_back(parseExpression());

                if (cur_.kind == tok_comma)
                {
                    next();
                    if (cur_.kind == tok_square_bracket_close)
                        break;
                    continue;
                }
                break;
            }
        }

        if (cur_.kind != tok_square_bracket_close)
        {
            throw ParseError("expected ']' to close list literal", cur_.location);
        }
        next();

        if (verbose_)
            printf("[parser] parsed list literal with %zu elements\n", elements.size());
        auto expr = std::make_unique<ListLiteralExpr>(std::move(elements));
        expr->location = listLoc;
        return expr;
    }

    if (cur_.kind == tok_number)
    {
        auto n = std::make_unique<NumberExprAST>(cur_.numberValue);
        next();
        return n;
    }
    if (cur_.kind == tok_float_literal)
    {
        auto f = std::make_unique<FloatLiteralExpr>(static_cast<float>(cur_.numberValue));
        next();
        return f;
    }
    if (cur_.kind == tok_string)
    {
        auto s = std::make_unique<StringExprAST>(cur_.text);
        next();
        return s;
    }
    if (cur_.kind == tok_char_literal)
    {
        char charValue = static_cast<char>(static_cast<int>(cur_.numberValue));
        auto c = std::make_unique<CharExprAST>(charValue);
        next();
        return c;
    }
    if (cur_.kind == tok_paren_open)
    {
        SourceLocation openLoc = cur_.location;
        next();
        bool isCast = isTypeToken(cur_);
        if (isCast)
        {
            std::string typeName = parseTypeString();
            if (cur_.kind != tok_paren_close)
            {
                throw ParseError("expected ')' after type in C-style cast", cur_.location);
            }
            next();
            auto operand = parsePrimary();
            return std::make_unique<CastExpr>(typeName, std::move(operand));
        }
        auto e = parseExpression();
        if (cur_.kind != tok_paren_close)
            throw ParseError("expected ')'", cur_.location);
        next();
        return e;
    }

    if (cur_.kind == tok_square_bracket_open)
    {
        next();

        if (cur_.kind == tok_square_bracket_close)
        {
            next();

            if (verbose_)
                printf("[parser] parsed empty array literal\n");
            return std::make_unique<ArrayLiteralExpr>(std::vector<std::unique_ptr<ExprAST>>());
        }

        auto firstExpr = parseExpression();

        if (cur_.kind == tok_colon)
        {
            throw ParseError("map literals must use '{ ... }'", cur_.location);
        }

        std::vector<std::unique_ptr<ExprAST>> elements;
        elements.push_back(std::move(firstExpr));

        while (cur_.kind == tok_comma)
        {
            next();
            if (cur_.kind == tok_square_bracket_close)
                break;
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

    if (cur_.kind == tok_brace_open)
    {
        SourceLocation mapLoc = cur_.location;
        next();

        std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<ExprAST>>> pairs;

        if (cur_.kind != tok_brace_close)
        {

            auto keyExpr = parseExpression();

            if (cur_.kind != tok_colon)
            {
                throw ParseError("expected ':' after map key, or use '[' and ']' for array literals", cur_.location);
            }
            next();
            auto valueExpr = parseExpression();
            pairs.emplace_back(std::move(keyExpr), std::move(valueExpr));

            while (cur_.kind == tok_comma)
            {
                next();
                if (cur_.kind == tok_brace_close)
                    break;

                auto key = parseExpression();
                if (cur_.kind != tok_colon)
                {
                    throw ParseError("expected ':' after map key", cur_.location);
                }
                next();
                auto value = parseExpression();
                pairs.emplace_back(std::move(key), std::move(value));
            }
        }

        if (cur_.kind != tok_brace_close)
        {
            throw ParseError("expected '}' to close map literal", cur_.location);
        }
        next();

        if (verbose_)
            printf("[parser] parsed standalone map literal with %zu pairs\n", pairs.size());
        auto expr = std::make_unique<MapLiteralExpr>(std::move(pairs));
        expr->location = mapLoc;
        return expr;
    }

    if (cur_.kind == tok_identifier)
    {
        std::string idName = cur_.text;
        SourceLocation idLoc = cur_.location;
        next();

        if (cur_.kind == tok_brace_open)
        {
            return parseStructLiteral(idName);
        }

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

    if (cur_.kind == tok_true)
    {
        next();
        return std::make_unique<BoolExprAST>(true);
    }
    if (cur_.kind == tok_false)
    {
        next();
        return std::make_unique<BoolExprAST>(false);
    }

    if (cur_.kind == tok_this)
    {
        next();
        return std::make_unique<VariableExprAST>("this");
    }

    if (cur_.kind == tok_null)
    {
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

    if (cur_.kind == tok_bitwise_or)
        return 6;

    if (cur_.kind == tok_bitwise_xor)
        return 7;

    if (cur_.kind == tok_ampersand)
        return 8;
    if (cur_.kind == tok_range)
        return 12;
    if (cur_.kind == tok_eq || cur_.kind == tok_ne)
        return 15;
    if (cur_.kind == tok_lt || cur_.kind == tok_gt || cur_.kind == tok_le || cur_.kind == tok_ge)
        return 17;

    if (cur_.kind == tok_shift_left || cur_.kind == tok_shift_right)
        return 18;
    if (cur_.kind == tok_plus || cur_.kind == tok_minus)
        return 20;
    if (cur_.kind == tok_mul || cur_.kind == tok_div || cur_.kind == tok_mod)
        return 30;
    if (cur_.kind == tok_dot)
        return 40;
    if (cur_.kind == tok_square_bracket_open)
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
    next();

    if (cur_.kind != tok_identifier)
        throw ParseError("expected struct name after 'struct'", cur_.location);

    std::string structName = cur_.text;
    next();

    std::string parentName = "";
    if (cur_.kind == tok_colon)
    {
        next();
        if (cur_.kind != tok_identifier)
        {
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
        if (cur_.kind == tok_data)
        {
            next();
            if (cur_.kind != tok_brace_open)
            {
                throw ParseError("expected '{' after 'data'", cur_.location);
            }
            next();

            while (cur_.kind != tok_brace_close && cur_.kind != tok_eof)
            {
                if (cur_.kind != tok_identifier)
                {
                    throw ParseError("expected field name in data block", cur_.location);
                }

                std::string fieldName = cur_.text;
                next();

                if (cur_.kind != tok_colon)
                {
                    throw ParseError("expected ':' after field name", cur_.location);
                }
                next();

                if (!isTypeToken(cur_) && !(cur_.kind == tok_identifier))
                {
                    throw ParseError("expected field type after ':'", cur_.location);
                }

                std::string fieldType = parseTypeString();

                fields.emplace_back(fieldName, fieldType);

                if (cur_.kind == tok_comma || cur_.kind == tok_semicolon)
                {
                    next();
                }
                else if (cur_.kind != tok_brace_close)
                {
                    throw ParseError("expected ',' or ';' or '}' after field declaration", cur_.location);
                }
            }

            if (cur_.kind != tok_brace_close)
            {
                throw ParseError("expected '}' to close data block", cur_.location);
            }
            next();
        }
        else if ((cur_.kind == tok_extend) || (cur_.kind == tok_impl) || isTypeToken(cur_) || cur_.kind == tok_identifier)
        {
            if (cur_.kind == tok_extend || cur_.kind == tok_impl)
            {
                next();
            }

            if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
            {
                throw ParseError("expected return type for method", cur_.location);
            }
            std::string returnType = parseTypeString();

            if (cur_.kind != tok_identifier)
            {
                throw ParseError("expected method name after return type", cur_.location);
            }
            auto methodName = cur_.text;
            next();

            if (cur_.kind != tok_paren_open)
            {
                throw ParseError("expected '(' after method name", cur_.location);
            }
            next();
            std::vector<std::pair<std::string, std::string>> params;
            if (cur_.kind != tok_paren_close)
            {
                while (true)
                {
                    if (cur_.kind != tok_identifier)
                    {
                        throw ParseError("expected parameter name", cur_.location);
                    }
                    std::string paramName = cur_.text;
                    next();
                    if (cur_.kind != tok_colon)
                    {
                        throw ParseError("expected ':' after parameter name", cur_.location);
                    }
                    next();
                    if (!isTypeToken(cur_) && cur_.kind != tok_identifier)
                    {
                        throw ParseError("expected parameter type after ':'", cur_.location);
                    }
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
            {
                throw ParseError("expected ')' after parameters", cur_.location);
            }
            next();
            if (cur_.kind != tok_brace_open)
            {
                throw ParseError("expected '{' to start method body", cur_.location);
            }
            next();

            std::vector<std::unique_ptr<StmtAST>> body;
            while (cur_.kind != tok_brace_close)
            {
                body.push_back(parseStatement());
            }
            next();
            methods.push_back(std::make_unique<FunctionAST>(methodName, returnType, params, std::move(body)));
        }
        else
        {
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
    next();

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
            std::string methodName = cur_.text;
            next();
            if (cur_.kind != tok_paren_open)
                throw ParseError("expected '(' after method name", cur_.location);
            next();
            std::vector<std::pair<std::string, std::string>> params;
            while (cur_.kind != tok_paren_close && cur_.kind != tok_eof)
            {
                if (cur_.kind != tok_identifier)
                    throw ParseError("expected parameter name", cur_.location);
                std::string paramName = cur_.text;
                next();
                if (cur_.kind != tok_colon)
                    throw ParseError("expected ':' after parameter name", cur_.location);
                next();
                if (!isTypeToken(cur_) && !(cur_.kind == tok_identifier))
                    throw ParseError("expected parameter type", cur_.location);
                std::string paramType = parseTypeString();
                params.emplace_back(paramName, paramType);
                if (cur_.kind == tok_comma)
                {
                    next();
                    continue;
                }
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
    next();

    if (cur_.kind != tok_identifier)
        throw ParseError("expected method name after '.'", cur_.location);

    std::string methodName = cur_.text;
    next();

    if (cur_.kind != tok_paren_open)
        throw ParseError("expected '(' after static method name", cur_.location);
    next();

    auto staticCall = std::make_unique<StaticCallExpr>(structName, methodName);

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
