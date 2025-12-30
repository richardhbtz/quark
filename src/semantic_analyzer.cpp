#include "../include/semantic_analyzer.h"
#include <algorithm>
#include <sstream>

bool Scope::declare(const std::string& name, Symbol symbol) {
    if (symbols_.find(name) != symbols_.end()) {
        return false;
    }
    symbols_[name] = std::move(symbol);
    return true;
}

Symbol* Scope::lookup(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return &it->second;
    }
    if (parent_) {
        return parent_->lookup(name);
    }
    return nullptr;
}

Symbol* Scope::lookupLocal(const std::string& name) {
    auto it = symbols_.find(name);
    return (it != symbols_.end()) ? &it->second : nullptr;
}

SymbolTable::SymbolTable() {
    globalScope_ = std::make_unique<Scope>(nullptr);
    currentScope_ = globalScope_.get();
}

void SymbolTable::enterScope() {
    auto newScope = std::make_unique<Scope>(currentScope_);
    currentScope_ = newScope.get();
    scopeStack_.push_back(std::move(newScope));
}

void SymbolTable::exitScope() {
    if (!scopeStack_.empty()) {
        currentScope_ = scopeStack_.back()->parent();
        scopeStack_.pop_back();
    }
}

bool SymbolTable::declare(const std::string& name, Symbol symbol) {
    return currentScope_->declare(name, std::move(symbol));
}

Symbol* SymbolTable::lookup(const std::string& name) {
    return currentScope_->lookup(name);
}

Symbol* SymbolTable::lookupLocal(const std::string& name) {
    return currentScope_->lookupLocal(name);
}

SemanticAnalyzer::SemanticAnalyzer(ErrorReporter& errorReporter, SourceManager& sourceManager,
                                   const std::string& filename, bool verbose)
    : errorReporter_(errorReporter), sourceManager_(sourceManager), 
      filename_(filename), verbose_(verbose) {
    if (auto file = sourceManager_.getFile(filename)) {
        sourceCode_ = file->content;
    }
    registerBuiltinFunctions();
}

void SemanticAnalyzer::registerBuiltinFunctions() {
    auto addBuiltin = [this](const std::string& name, const std::string& returnType, 
                             std::vector<std::pair<std::string, std::string>> params, bool variadic) {
        Symbol sym;
        sym.kind = Symbol::Kind::Function;
        sym.name = name;
        sym.returnType = returnType;
        sym.functionParams = std::move(params);
        sym.isVariadic = variadic;
        sym.isExtern = true;
        sym.resolvedType = resolveType(returnType).type;
        symbolTable_.globalScope()->declare(name, std::move(sym));
    };
    
    addBuiltin("print", "void", {}, true);
    addBuiltin("println", "void", {}, true);
    addBuiltin("readline", "str", {}, true);
    addBuiltin("format", "str", {}, true);
    addBuiltin("to_string", "str", {}, true);
    addBuiltin("to_int", "int", {}, true);
    addBuiltin("str_len", "int", {{"s", "str"}}, false);
    addBuiltin("str_slice", "str", {{"s", "str"}, {"start", "int"}, {"end", "int"}}, false);
    addBuiltin("str_concat", "str", {{"a", "str"}, {"b", "str"}}, false);
    addBuiltin("str_find", "int", {{"haystack", "str"}, {"needle", "str"}}, false);
    addBuiltin("str_replace", "str", {{"s", "str"}, {"old", "str"}, {"new", "str"}}, false);
    addBuiltin("str_split", "str[]", {{"s", "str"}, {"delim", "str"}}, false);
    addBuiltin("sin", "double", {{"x", "double"}}, false);
    addBuiltin("cos", "double", {{"x", "double"}}, false);
    addBuiltin("tan", "double", {{"x", "double"}}, false);
    addBuiltin("sqrt", "double", {{"x", "double"}}, false);
    addBuiltin("pow", "double", {{"base", "double"}, {"exp", "double"}}, false);
    addBuiltin("log", "double", {{"x", "double"}}, false);
    addBuiltin("log10", "double", {{"x", "double"}}, false);
    addBuiltin("exp", "double", {{"x", "double"}}, false);
    addBuiltin("abs", "double", {{"x", "double"}}, false);
    addBuiltin("floor", "double", {{"x", "double"}}, false);
    addBuiltin("ceil", "double", {{"x", "double"}}, false);
    addBuiltin("round", "double", {{"x", "double"}}, false);
    addBuiltin("fmod", "double", {{"x", "double"}, {"y", "double"}}, false);
    addBuiltin("atan2", "double", {{"y", "double"}, {"x", "double"}}, false);
    addBuiltin("asin", "double", {{"x", "double"}}, false);
    addBuiltin("acos", "double", {{"x", "double"}}, false);
    addBuiltin("atan", "double", {{"x", "double"}}, false);
    addBuiltin("sinh", "double", {{"x", "double"}}, false);
    addBuiltin("cosh", "double", {{"x", "double"}}, false);
    addBuiltin("tanh", "double", {{"x", "double"}}, false);
    addBuiltin("sleep", "void", {{"ms", "int"}}, false);
    
    addBuiltin("abs_i32", "int", {{"x", "int"}}, false);
    addBuiltin("abs_f64", "double", {{"x", "double"}}, false);
    addBuiltin("min_i32", "int", {{"a", "int"}, {"b", "int"}}, false);
    addBuiltin("max_i32", "int", {{"a", "int"}, {"b", "int"}}, false);
    addBuiltin("min_f64", "double", {{"a", "double"}, {"b", "double"}}, false);
    addBuiltin("max_f64", "double", {{"a", "double"}, {"b", "double"}}, false);
    addBuiltin("min", "double", {}, true);
    addBuiltin("max", "double", {}, true);
    addBuiltin("clamp_i32", "int", {{"x", "int"}, {"lo", "int"}, {"hi", "int"}}, false);
    addBuiltin("clamp_f64", "double", {{"x", "double"}, {"lo", "double"}, {"hi", "double"}}, false);
    addBuiltin("clamp", "double", {}, true);
}

bool SemanticAnalyzer::analyze(ProgramAST* program) {
    if (!program) return false;
    
    collectDeclarations(program);
    
    bool hasActualErrors = std::any_of(errors_.begin(), errors_.end(), 
                                        [](const SemanticError& e) { return !e.isWarning; });
    
    if (hasActualErrors) {
        for (const auto& err : errors_) {
            if (err.isWarning) {
                errorReporter_.reportWarning(err.message, err.location, sourceCode_, err.errorCode, err.spanLength);
            } else {
                errorReporter_.reportParseError(err.message, err.location, sourceCode_, err.errorCode, err.spanLength);
            }
        }
        return false;
    }
    
    analyzeStatements(program);
    
    for (const auto& err : errors_) {
        if (err.isWarning) {
            errorReporter_.reportWarning(err.message, err.location, sourceCode_, err.errorCode, err.spanLength);
        } else {
            errorReporter_.reportParseError(err.message, err.location, sourceCode_, err.errorCode, err.spanLength);
        }
    }
    
    hasActualErrors = std::any_of(errors_.begin(), errors_.end(), 
                                   [](const SemanticError& e) { return !e.isWarning; });
    return !hasActualErrors;
}

void SemanticAnalyzer::collectDeclarations(ProgramAST* program) {
    std::function<void(const std::vector<std::unique_ptr<StmtAST>>&)> collectStructs;
    std::function<void(const std::vector<std::unique_ptr<StmtAST>>&)> collectFuncsAndExterns;
    
    collectStructs = [&](const std::vector<std::unique_ptr<StmtAST>>& stmts) {
        for (auto& stmt : stmts) {
            if (auto* inc = dynamic_cast<IncludeStmt*>(stmt.get())) {
                collectStructs(inc->stmts);
            } else if (auto* structDef = dynamic_cast<StructDefStmt*>(stmt.get())) {
                collectStructDef(structDef);
            }
        }
    };
    
    collectFuncsAndExterns = [&](const std::vector<std::unique_ptr<StmtAST>>& stmts) {
        for (auto& stmt : stmts) {
            if (auto* inc = dynamic_cast<IncludeStmt*>(stmt.get())) {
                collectFuncsAndExterns(inc->stmts);
            } else if (auto* func = dynamic_cast<FunctionAST*>(stmt.get())) {
                collectFunction(func);
            } else if (auto* externFunc = dynamic_cast<ExternFunctionAST*>(stmt.get())) {
                collectExternFunction(externFunc);
            } else if (auto* impl = dynamic_cast<ImplStmt*>(stmt.get())) {
                collectImplBlock(impl);
            }
        }
    };
    
    collectStructs(program->stmts);
    collectFuncsAndExterns(program->stmts);
}

void SemanticAnalyzer::collectStructDef(StructDefStmt* stmt) {
    Symbol sym;
    sym.kind = Symbol::Kind::Struct;
    sym.name = stmt->name;
    sym.typeName = stmt->name;
    sym.resolvedType = QuarkType::Struct;
    sym.declLocation = stmt->location;
    sym.structFields = stmt->fields;
    sym.parentStruct = stmt->parentName;
    
    for (auto& method : stmt->methods) {
        sym.methodNames.push_back(method->name);
    }
    
    if (!symbolTable_.declare(stmt->name, sym)) {
        error("struct '" + stmt->name + "' is already defined", stmt->location, "E101", stmt->name.size());
        return;
    }
    
    if (!stmt->parentName.empty()) {
        Symbol* parent = symbolTable_.lookup(stmt->parentName);
        if (!parent || parent->kind != Symbol::Kind::Struct) {
            error("parent struct '" + stmt->parentName + "' not found", stmt->location, "E102", stmt->parentName.size());
        }
    }
    
    for (auto& method : stmt->methods) {
        std::string methodName = stmt->name + "::" + method->name;
        auto originalName = method->name;
        method->name = methodName;
        collectFunction(method.get());
        method->name = originalName;
    }
}

void SemanticAnalyzer::collectFunction(FunctionAST* func) {
    std::string funcName = func->name;
    
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.name = funcName;
    sym.returnType = func->returnType;
    sym.functionParams = func->params;
    sym.declLocation = func->location;
    sym.resolvedType = resolveType(func->returnType).type;
    
    for (const auto& param : func->params) {
        if (param.second == "...") {
            sym.isVariadic = true;
            break;
        }
    }
    
    size_t colonPos = funcName.find("::");
    if (colonPos != std::string::npos) {
        sym.isMethod = true;
        sym.structName = funcName.substr(0, colonPos);
    }
    
    Symbol* existing = symbolTable_.globalScope()->lookupLocal(funcName);
    if (existing) {
        if (existing->isExtern && !sym.isExtern) {
            symbolTable_.globalScope()->symbols().erase(funcName);
        } else if (!existing->isExtern) {
            error("function '" + funcName + "' is already defined", func->location, "E103", funcName.size());
            return;
        }
    }
    
    symbolTable_.globalScope()->declare(funcName, sym);
}

void SemanticAnalyzer::collectExternFunction(ExternFunctionAST* func) {
    Symbol sym;
    sym.kind = Symbol::Kind::Function;
    sym.name = func->name;
    sym.returnType = func->returnType;
    sym.functionParams = func->params;
    sym.declLocation = func->location;
    sym.isExtern = true;
    sym.resolvedType = resolveType(func->returnType).type;
    
    symbolTable_.globalScope()->declare(func->name, sym);
}

void SemanticAnalyzer::collectImplBlock(ImplStmt* impl) {
    Symbol* structSym = symbolTable_.lookup(impl->structName);
    if (!structSym || structSym->kind != Symbol::Kind::Struct) {
        error("cannot implement methods for unknown struct '" + impl->structName + "'", 
              impl->location, "E104", impl->structName.size());
        return;
    }
    
    for (auto& method : impl->methods) {
        std::string methodName = impl->structName + "::" + method->name;
        method->name = methodName;
        collectFunction(method.get());
        structSym->methodNames.push_back(method->name);
    }
}

void SemanticAnalyzer::analyzeStatements(ProgramAST* program) {
    for (auto& stmt : program->stmts) {
        analyzeStmt(stmt.get());
    }
}

void SemanticAnalyzer::analyzeStmt(StmtAST* stmt) {
    if (!stmt) return;
    
    if (auto* inc = dynamic_cast<IncludeStmt*>(stmt)) {
        analyzeInclude(inc);
    } else if (auto* func = dynamic_cast<FunctionAST*>(stmt)) {
        analyzeFunction(func);
    } else if (auto* varDecl = dynamic_cast<VarDeclStmt*>(stmt)) {
        analyzeVarDecl(varDecl);
    } else if (auto* assign = dynamic_cast<AssignStmtAST*>(stmt)) {
        analyzeAssign(assign);
    } else if (auto* memberAssign = dynamic_cast<MemberAssignStmt*>(stmt)) {
        analyzeMemberAssign(memberAssign);
    } else if (auto* arrayAssign = dynamic_cast<ArrayAssignStmt*>(stmt)) {
        analyzeArrayAssign(arrayAssign);
    } else if (auto* derefAssign = dynamic_cast<DerefAssignStmt*>(stmt)) {
        analyzeDerefAssign(derefAssign);
    } else if (auto* ifStmt = dynamic_cast<IfStmtAST*>(stmt)) {
        analyzeIf(ifStmt);
    } else if (auto* whileStmt = dynamic_cast<WhileStmt*>(stmt)) {
        analyzeWhile(whileStmt);
    } else if (auto* forStmt = dynamic_cast<ForStmt*>(stmt)) {
        analyzeFor(forStmt);
    } else if (auto* retStmt = dynamic_cast<ReturnStmt*>(stmt)) {
        analyzeReturn(retStmt);
    } else if (auto* exprStmt = dynamic_cast<ExprStmtAST*>(stmt)) {
        analyzeExprStmt(exprStmt);
    } else if (auto* structDef = dynamic_cast<StructDefStmt*>(stmt)) {
        analyzeStructDef(structDef);
    } else if (auto* impl = dynamic_cast<ImplStmt*>(stmt)) {
        analyzeImplBlock(impl);
    } else if (dynamic_cast<BreakStmt*>(stmt)) {
        if (!inLoop_) {
            error("'break' statement outside of loop", stmt->location, "E105");
        }
    } else if (dynamic_cast<ContinueStmt*>(stmt)) {
        if (!inLoop_) {
            error("'continue' statement outside of loop", stmt->location, "E106");
        }
    }
}

void SemanticAnalyzer::analyzeFunction(FunctionAST* func) {
    currentFunctionName_ = func->name;
    currentFunctionReturnType_ = func->returnType;
    hasReturn_ = false;
    
    symbolTable_.enterScope();
    
    size_t colonPos = func->name.find("::");
    std::string structName;
    std::string methodName;
    
    if (colonPos != std::string::npos) {
        structName = func->name.substr(0, colonPos);
        methodName = func->name.substr(colonPos + 2);
        currentStructName_ = structName;
        
        bool isConstructor = (methodName == "new" && func->returnType == structName);
        if (!isConstructor) {
            Symbol thisSym;
            thisSym.kind = Symbol::Kind::Parameter;
            thisSym.name = "this";
            thisSym.typeName = structName + "*";
            thisSym.resolvedType = QuarkType::Pointer;
            thisSym.structName = structName;
            thisSym.isInitialized = true;
            symbolTable_.declare("this", thisSym);
            
            auto allFields = getStructFields(structName);
            for (const auto& field : allFields) {
                Symbol fieldSym;
                fieldSym.kind = Symbol::Kind::Field;
                fieldSym.name = field.first;
                fieldSym.typeName = field.second;
                TypeInfo fieldTypeInfo = resolveType(field.second);
                fieldSym.resolvedType = fieldTypeInfo.type;
                fieldSym.structName = fieldTypeInfo.structName;
                fieldSym.isInitialized = true;
                symbolTable_.declare(field.first, fieldSym);
            }
        }
    }
    
    for (const auto& param : func->params) {
        if (param.second == "...") continue;
        
        Symbol paramSym;
        paramSym.kind = Symbol::Kind::Parameter;
        paramSym.name = param.first;
        paramSym.typeName = param.second;
        TypeInfo paramTypeInfo = resolveType(param.second);
        paramSym.resolvedType = paramTypeInfo.type;
        paramSym.structName = paramTypeInfo.structName;
        paramSym.isInitialized = true;
        paramSym.declLocation = func->location;
        
        if (!symbolTable_.declare(param.first, paramSym)) {
            error("duplicate parameter name '" + param.first + "'", func->location, "E107", param.first.size());
        }
    }
    
    for (auto& stmt : func->body) {
        analyzeStmt(stmt.get());
    }
    
    if (func->returnType != "void" && !hasReturn_) {
        warning("function '" + func->name + "' may not return a value on all paths", 
                func->location, "W001", func->name.size());
    }
    
    symbolTable_.exitScope();
    currentFunctionName_.clear();
    currentFunctionReturnType_.clear();
    if (colonPos != std::string::npos) {
        currentStructName_.clear();
    }
}

void SemanticAnalyzer::analyzeVarDecl(VarDeclStmt* stmt) {
    TypeInfo initType;
    if (stmt->init) {
        initType = analyzeExpr(stmt->init.get());
    }
    
    std::string declaredType = stmt->type;
    if (declaredType == "var" || declaredType == "auto" || declaredType.empty()) {
        if (!stmt->init) {
            error("type inference requires an initializer", stmt->location, "E108");
            return;
        }
        declaredType = typeToString(initType);
        if (verbose_) {
            printf("[semantic] var inference: %s inferred as %s (structName=%s)\n", 
                   stmt->name.c_str(), declaredType.c_str(), initType.structName.c_str());
        }
    }
    
    TypeInfo targetType = resolveType(declaredType);
    
    if (stmt->init && !isTypeCompatible(targetType, initType)) {
        error("cannot initialize variable of type '" + declaredType + 
              "' with value of type '" + typeToString(initType) + "'",
              stmt->location, "E109");
    }
    
    Symbol varSym;
    varSym.kind = Symbol::Kind::Variable;
    varSym.name = stmt->name;
    varSym.typeName = declaredType;
    varSym.resolvedType = targetType.type;
    varSym.structName = targetType.structName;
    varSym.elementType = targetType.elementType;
    varSym.isInitialized = (stmt->init != nullptr);
    varSym.declLocation = stmt->location;
    
    if (verbose_) {
        printf("[semantic] declared var %s: type=%d structName=%s\n", 
               stmt->name.c_str(), (int)varSym.resolvedType, varSym.structName.c_str());
    }
    
    if (!symbolTable_.declare(stmt->name, varSym)) {
        error("variable '" + stmt->name + "' is already defined in this scope", 
              stmt->location, "E110", stmt->name.size());
    }
}

void SemanticAnalyzer::analyzeAssign(AssignStmtAST* stmt) {
    Symbol* var = symbolTable_.lookup(stmt->varName);
    TypeInfo valueType = analyzeExpr(stmt->value.get());
    
    if (!var) {
        Symbol newVar;
        newVar.kind = Symbol::Kind::Variable;
        newVar.name = stmt->varName;
        newVar.typeName = typeToString(valueType);
        newVar.resolvedType = valueType.type;
        newVar.structName = valueType.structName;
        newVar.elementType = valueType.elementType;
        newVar.isInitialized = true;
        newVar.declLocation = stmt->location;
        symbolTable_.declare(stmt->varName, newVar);
        return;
    }
    
    TypeInfo varType = resolveType(var->typeName);
    
    if (!isTypeCompatible(varType, valueType)) {
        error("cannot assign value of type '" + typeToString(valueType) + 
              "' to variable of type '" + var->typeName + "'",
              stmt->location, "E112");
    }
    
    var->isInitialized = true;
}

void SemanticAnalyzer::analyzeMemberAssign(MemberAssignStmt* stmt) {
    TypeInfo objType = analyzeExpr(stmt->object.get());
    
    if (objType.type != QuarkType::Struct && objType.type != QuarkType::Pointer) {
        error("member access requires struct type", stmt->location, "E113");
        return;
    }
    
    std::string structName = objType.structName;
    auto fields = getStructFields(structName);
    
    bool found = false;
    std::string fieldType;
    for (const auto& field : fields) {
        if (field.first == stmt->fieldName) {
            found = true;
            fieldType = field.second;
            break;
        }
    }
    
    if (!found) {
        error("struct '" + structName + "' has no field named '" + stmt->fieldName + "'",
              stmt->location, "E114", stmt->fieldName.size());
        return;
    }
    
    TypeInfo valueType = analyzeExpr(stmt->value.get());
    TypeInfo targetType = resolveType(fieldType);
    
    if (!isTypeCompatible(targetType, valueType)) {
        error("cannot assign value of type '" + typeToString(valueType) + 
              "' to field of type '" + fieldType + "'",
              stmt->location, "E115");
    }
}

void SemanticAnalyzer::analyzeArrayAssign(ArrayAssignStmt* stmt) {
    TypeInfo arrayType = analyzeExpr(stmt->array.get());
    
    if (arrayType.type != QuarkType::Array && arrayType.type != QuarkType::Pointer) {
        error("subscript operator requires array or pointer type", stmt->location, "E116");
        return;
    }
    
    TypeInfo indexType = analyzeExpr(stmt->index.get());
    if (indexType.type != QuarkType::Int) {
        error("array index must be an integer", stmt->location, "E117");
    }
    
    TypeInfo valueType = analyzeExpr(stmt->value.get());
    
    TypeInfo elementType;
    elementType.type = arrayType.elementType;
    elementType.structName = arrayType.structName;
    
    if (!isTypeCompatible(elementType, valueType)) {
        error("cannot assign value of type '" + typeToString(valueType) + 
              "' to array element", stmt->location, "E118");
    }
}

void SemanticAnalyzer::analyzeDerefAssign(DerefAssignStmt* stmt) {
    TypeInfo ptrType = analyzeExpr(stmt->deref->operand.get());
    
    if (ptrType.type != QuarkType::Pointer) {
        error("cannot dereference non-pointer type", stmt->location, "E119");
        return;
    }
    
    analyzeExpr(stmt->value.get());
}

void SemanticAnalyzer::analyzeIf(IfStmtAST* stmt) {
    TypeInfo condType = analyzeExpr(stmt->cond.get());
    if (condType.type != QuarkType::Boolean && condType.type != QuarkType::Int) {
        error("condition must be a boolean or integer expression", stmt->location, "E120");
    }
    
    symbolTable_.enterScope();
    for (auto& s : stmt->thenBody) {
        analyzeStmt(s.get());
    }
    symbolTable_.exitScope();
    
    for (auto& elif : stmt->elifs) {
        TypeInfo elifCondType = analyzeExpr(elif.first.get());
        if (elifCondType.type != QuarkType::Boolean && elifCondType.type != QuarkType::Int) {
            error("condition must be a boolean or integer expression", elif.first->location, "E120");
        }
        
        symbolTable_.enterScope();
        for (auto& s : elif.second) {
            analyzeStmt(s.get());
        }
        symbolTable_.exitScope();
    }
    
    if (!stmt->elseBody.empty()) {
        symbolTable_.enterScope();
        for (auto& s : stmt->elseBody) {
            analyzeStmt(s.get());
        }
        symbolTable_.exitScope();
    }
}

void SemanticAnalyzer::analyzeWhile(WhileStmt* stmt) {
    TypeInfo condType = analyzeExpr(stmt->condition.get());
    if (condType.type != QuarkType::Boolean && condType.type != QuarkType::Int) {
        error("condition must be a boolean or integer expression", stmt->location, "E120");
    }
    
    bool wasInLoop = inLoop_;
    inLoop_ = true;
    
    symbolTable_.enterScope();
    for (auto& s : stmt->body) {
        analyzeStmt(s.get());
    }
    symbolTable_.exitScope();
    
    inLoop_ = wasInLoop;
}

void SemanticAnalyzer::analyzeFor(ForStmt* stmt) {
    symbolTable_.enterScope();
    
    Symbol loopVar;
    loopVar.kind = Symbol::Kind::Variable;
    loopVar.name = stmt->var;
    loopVar.typeName = "int";
    loopVar.resolvedType = QuarkType::Int;
    loopVar.isInitialized = true;
    loopVar.declLocation = stmt->location;
    symbolTable_.declare(stmt->var, loopVar);
    
    if (stmt->rangeExpr) {
        analyzeExpr(stmt->rangeExpr.get());
    }
    
    bool wasInLoop = inLoop_;
    inLoop_ = true;
    
    for (auto& s : stmt->body) {
        analyzeStmt(s.get());
    }
    
    inLoop_ = wasInLoop;
    symbolTable_.exitScope();
}

void SemanticAnalyzer::analyzeReturn(ReturnStmt* stmt) {
    hasReturn_ = true;
    
    if (currentFunctionReturnType_.empty()) {
        error("return statement outside of function", stmt->location, "E121");
        return;
    }
    
    if (currentFunctionReturnType_ == "void") {
        if (stmt->returnValue) {
            error("void function should not return a value", stmt->location, "E122");
        }
        return;
    }
    
    if (!stmt->returnValue) {
        error("non-void function must return a value", stmt->location, "E123");
        return;
    }
    
    TypeInfo returnType = analyzeExpr(stmt->returnValue.get());
    TypeInfo expectedType = resolveType(currentFunctionReturnType_);
    
    if (!isTypeCompatible(expectedType, returnType)) {
        error("cannot return value of type '" + typeToString(returnType) + 
              "' from function with return type '" + currentFunctionReturnType_ + "'",
              stmt->location, "E124");
    }
}

void SemanticAnalyzer::analyzeExprStmt(ExprStmtAST* stmt) {
    analyzeExpr(stmt->expr.get());
}

void SemanticAnalyzer::analyzeStructDef(StructDefStmt* stmt) {
    currentStructName_ = stmt->name;
    for (auto& method : stmt->methods) {
        std::string originalName = method->name;
        std::string qualifiedName = stmt->name + "::" + method->name;
        method->name = qualifiedName;
        analyzeFunction(method.get());
        method->name = originalName;
    }
    currentStructName_.clear();
}

void SemanticAnalyzer::analyzeImplBlock(ImplStmt* stmt) {
    currentStructName_ = stmt->structName;
    for (auto& method : stmt->methods) {
        analyzeFunction(method.get());
    }
    currentStructName_.clear();
}

void SemanticAnalyzer::analyzeInclude(IncludeStmt* stmt) {
    for (auto& s : stmt->stmts) {
        analyzeStmt(s.get());
    }
}

TypeInfo SemanticAnalyzer::analyzeExpr(ExprAST* expr) {
    if (!expr) return TypeInfo(QuarkType::Unknown);
    
    if (auto* num = dynamic_cast<NumberExprAST*>(expr)) {
        double val = num->value;
        if (val == static_cast<int>(val)) {
            return TypeInfo(QuarkType::Int, expr->location);
        }
        return TypeInfo(QuarkType::Double, expr->location);
    }
    
    if (auto* str = dynamic_cast<StringExprAST*>(expr)) {
        return TypeInfo(QuarkType::String, expr->location);
    }
    
    if (auto* boolean = dynamic_cast<BoolExprAST*>(expr)) {
        return TypeInfo(QuarkType::Boolean, expr->location);
    }
    
    if (auto* var = dynamic_cast<VariableExprAST*>(expr)) {
        Symbol* sym = symbolTable_.lookup(var->name);
        if (!sym) {
            error("undefined variable '" + var->name + "'", expr->location, "E111", var->name.size());
            return TypeInfo(QuarkType::Unknown, expr->location);
        }
        
        if (!sym->isInitialized && sym->kind == Symbol::Kind::Variable) {
            warning("variable '" + var->name + "' may be used uninitialized", 
                    expr->location, "W002", var->name.size());
        }
        
        TypeInfo info(sym->resolvedType, expr->location, sym->structName);
        info.elementType = sym->elementType;
        return info;
    }
    
    if (auto* call = dynamic_cast<CallExprAST*>(expr)) {
        return analyzeCall(call);
    }
    
    if (auto* methodCall = dynamic_cast<MethodCallExpr*>(expr)) {
        return analyzeMethodCall(methodCall);
    }
    
    if (auto* staticCall = dynamic_cast<StaticCallExpr*>(expr)) {
        return analyzeStaticCall(staticCall);
    }
    
    if (auto* memberAccess = dynamic_cast<MemberAccessExpr*>(expr)) {
        return analyzeMemberAccess(memberAccess);
    }
    
    if (auto* arrayAccess = dynamic_cast<ArrayAccessExpr*>(expr)) {
        return analyzeArrayAccess(arrayAccess);
    }
    
    if (auto* binary = dynamic_cast<BinaryExprAST*>(expr)) {
        return analyzeBinary(binary);
    }
    
    if (auto* unary = dynamic_cast<UnaryExprAST*>(expr)) {
        return analyzeUnary(unary);
    }
    
    if (auto* structLit = dynamic_cast<StructLiteralExpr*>(expr)) {
        return analyzeStructLiteral(structLit);
    }
    
    if (auto* arrayLit = dynamic_cast<ArrayLiteralExpr*>(expr)) {
        return analyzeArrayLiteral(arrayLit);
    }
    
    if (auto* cast = dynamic_cast<CastExpr*>(expr)) {
        return analyzeCast(cast);
    }
    
    if (auto* addrOf = dynamic_cast<AddressOfExpr*>(expr)) {
        return analyzeAddressOf(addrOf);
    }
    
    if (auto* deref = dynamic_cast<DereferenceExpr*>(expr)) {
        return analyzeDereference(deref);
    }
    
    if (auto* range = dynamic_cast<RangeExpr*>(expr)) {
        analyzeExpr(range->start.get());
        analyzeExpr(range->end.get());
        return TypeInfo(QuarkType::Array, expr->location, "", QuarkType::Int);
    }
    
    return TypeInfo(QuarkType::Unknown, expr->location);
}

TypeInfo SemanticAnalyzer::analyzeCall(CallExprAST* expr) {
    Symbol* func = symbolTable_.lookup(expr->callee);
    if (!func) {
        error("undefined function '" + expr->callee + "'", expr->location, "E125", expr->callee.size());
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    if (func->kind != Symbol::Kind::Function) {
        error("'" + expr->callee + "' is not a function", expr->location, "E126", expr->callee.size());
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    size_t expectedParams = func->functionParams.size();
    if (func->isVariadic && expectedParams > 0) {
        expectedParams--;
    }
    
    if (!func->isVariadic && expr->args.size() != func->functionParams.size()) {
        error("function '" + expr->callee + "' expects " + std::to_string(func->functionParams.size()) +
              " arguments, got " + std::to_string(expr->args.size()),
              expr->location, "E127");
    } else if (func->isVariadic && expr->args.size() < expectedParams) {
        error("function '" + expr->callee + "' expects at least " + std::to_string(expectedParams) +
              " arguments, got " + std::to_string(expr->args.size()),
              expr->location, "E127");
    }
    
    for (size_t i = 0; i < expr->args.size() && i < func->functionParams.size(); ++i) {
        if (func->functionParams[i].second == "...") break;
        
        TypeInfo argType = analyzeExpr(expr->args[i].get());
        TypeInfo paramType = resolveType(func->functionParams[i].second);
        
        if (!isTypeCompatible(paramType, argType)) {
            error("argument " + std::to_string(i + 1) + " type mismatch: expected '" + 
                  func->functionParams[i].second + "', got '" + typeToString(argType) + "'",
                  expr->args[i]->location, "E128");
        }
    }
    
    for (size_t i = func->functionParams.size(); i < expr->args.size(); ++i) {
        analyzeExpr(expr->args[i].get());
    }
    
    TypeInfo retType = resolveType(func->returnType);
    retType.location = expr->location;
    return retType;
}

TypeInfo SemanticAnalyzer::analyzeMethodCall(MethodCallExpr* expr) {
    TypeInfo objType = analyzeExpr(expr->object.get());
    
    if (verbose_) {
        printf("[semantic] method call %s on type=%d structName=%s\n", 
               expr->methodName.c_str(), (int)objType.type, objType.structName.c_str());
    }
    
    std::string structName;
    if (objType.type == QuarkType::Struct) {
        structName = objType.structName;
    } else if (objType.type == QuarkType::Pointer) {
        structName = objType.structName;
    } else if (objType.type == QuarkType::Array) {
        if (expr->methodName == "length" || expr->methodName == "slice" || 
            expr->methodName == "push" || expr->methodName == "pop" || expr->methodName == "free") {
            for (auto& arg : expr->args) {
                analyzeExpr(arg.get());
            }
            if (expr->methodName == "length") {
                return TypeInfo(QuarkType::Int, expr->location);
            } else if (expr->methodName == "slice" || expr->methodName == "push") {
                return objType;
            } else if (expr->methodName == "free") {
                return TypeInfo(QuarkType::Void, expr->location);
            }
            return TypeInfo(objType.elementType, expr->location);
        }
        error("arrays only support 'length', 'slice', 'push', 'pop', 'free' methods", expr->location, "E129");
        return TypeInfo(QuarkType::Unknown, expr->location);
    } else if (objType.type == QuarkType::String) {
        if (expr->methodName == "length" || expr->methodName == "substring" ||
            expr->methodName == "charAt" || expr->methodName == "indexOf" ||
            expr->methodName == "slice" || expr->methodName == "find" ||
            expr->methodName == "split" || expr->methodName == "concat" ||
            expr->methodName == "replace") {
            for (auto& arg : expr->args) {
                analyzeExpr(arg.get());
            }
            if (expr->methodName == "length" || expr->methodName == "indexOf") {
                return TypeInfo(QuarkType::Int, expr->location);
            } else if (expr->methodName == "charAt") {
                return TypeInfo(QuarkType::String, expr->location);
            } else if (expr->methodName == "find") {
                return TypeInfo(QuarkType::Boolean, expr->location);
            } else if (expr->methodName == "split") {
                TypeInfo arrType(QuarkType::Array, expr->location);
                arrType.elementType = QuarkType::String;
                return arrType;
            }
            return TypeInfo(QuarkType::String, expr->location);
        }
    }
    
    if (structName.empty()) {
        error("method call on non-struct type", expr->location, "E130");
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    Symbol* method = findMethod(structName, expr->methodName);
    if (!method) {
        error("struct '" + structName + "' has no method named '" + expr->methodName + "'",
              expr->location, "E131", expr->methodName.size());
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    size_t expectedArgs = method->functionParams.size();
    
    if (expr->args.size() != expectedArgs) {
        error("method '" + expr->methodName + "' expects " + std::to_string(expectedArgs) +
              " arguments, got " + std::to_string(expr->args.size()),
              expr->location, "E127");
    }
    
    for (size_t i = 0; i < expr->args.size(); ++i) {
        analyzeExpr(expr->args[i].get());
    }
    
    TypeInfo retType = resolveType(method->returnType);
    retType.location = expr->location;
    return retType;
}

TypeInfo SemanticAnalyzer::analyzeStaticCall(StaticCallExpr* expr) {
    Symbol* structSym = symbolTable_.lookup(expr->structName);
    if (!structSym || structSym->kind != Symbol::Kind::Struct) {
        error("unknown struct '" + expr->structName + "'", expr->location, "E132", expr->structName.size());
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    std::string methodName = expr->structName + "::" + expr->methodName;
    Symbol* method = symbolTable_.lookup(methodName);
    if (!method) {
        error("struct '" + expr->structName + "' has no static method '" + expr->methodName + "'",
              expr->location, "E133", expr->methodName.size());
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    for (auto& arg : expr->args) {
        analyzeExpr(arg.get());
    }
    
    TypeInfo retType = resolveType(method->returnType);
    retType.location = expr->location;
    return retType;
}

TypeInfo SemanticAnalyzer::analyzeMemberAccess(MemberAccessExpr* expr) {
    TypeInfo objType = analyzeExpr(expr->object.get());
    
    std::string structName;
    if (objType.type == QuarkType::Struct) {
        structName = objType.structName;
    } else if (objType.type == QuarkType::Pointer) {
        structName = objType.structName;
    } else {
        error("member access requires struct type", expr->location, "E113");
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    auto fields = getStructFields(structName);
    for (const auto& field : fields) {
        if (field.first == expr->fieldName) {
            TypeInfo fieldType = resolveType(field.second);
            fieldType.location = expr->location;
            return fieldType;
        }
    }
    
    error("struct '" + structName + "' has no field named '" + expr->fieldName + "'",
          expr->location, "E114", expr->fieldName.size());
    return TypeInfo(QuarkType::Unknown, expr->location);
}

TypeInfo SemanticAnalyzer::analyzeArrayAccess(ArrayAccessExpr* expr) {
    TypeInfo arrayType = analyzeExpr(expr->array.get());
    
    if (arrayType.type != QuarkType::Array && arrayType.type != QuarkType::Pointer &&
        arrayType.type != QuarkType::String) {
        error("subscript operator requires array, pointer, or string type", expr->location, "E116");
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    TypeInfo indexType = analyzeExpr(expr->index.get());
    if (indexType.type != QuarkType::Int) {
        error("array index must be an integer", expr->location, "E117");
    }
    
    if (arrayType.type == QuarkType::String) {
        return TypeInfo(QuarkType::String, expr->location);
    }
    
    TypeInfo elemType;
    elemType.type = arrayType.elementType;
    elemType.structName = arrayType.structName;
    elemType.location = expr->location;
    return elemType;
}

TypeInfo SemanticAnalyzer::analyzeBinary(BinaryExprAST* expr) {
    TypeInfo lhsType = analyzeExpr(expr->lhs.get());
    TypeInfo rhsType = analyzeExpr(expr->rhs.get());
    
    switch (expr->op) {
        case '+':
        case '-':
        case '*':
        case '/':
        case '%':
            if (lhsType.type == QuarkType::String && expr->op == '+') {
                return TypeInfo(QuarkType::String, expr->location);
            }
            if (!IntegerTypeUtils::isNumericType(lhsType.type) || 
                !IntegerTypeUtils::isNumericType(rhsType.type)) {
                error("arithmetic operators require numeric operands", expr->location, "E134");
            }
            if (lhsType.type == QuarkType::Double || rhsType.type == QuarkType::Double) {
                return TypeInfo(QuarkType::Double, expr->location);
            }
            if (lhsType.type == QuarkType::Float || rhsType.type == QuarkType::Float) {
                return TypeInfo(QuarkType::Float, expr->location);
            }
            return TypeInfo(QuarkType::Int, expr->location);
            
        case '<':
        case '>':
        case 'l':
        case 'g':
        case '=':
        case '!':
        case 'n':
            return TypeInfo(QuarkType::Boolean, expr->location);
            
        case '&':
        case '|':
            if (lhsType.type != QuarkType::Boolean || rhsType.type != QuarkType::Boolean) {
                if (!IntegerTypeUtils::isNumericType(lhsType.type) || 
                    !IntegerTypeUtils::isNumericType(rhsType.type)) {
                    error("logical operators require boolean or numeric operands", expr->location, "E135");
                }
            }
            return TypeInfo(QuarkType::Boolean, expr->location);
            
        default:
            return TypeInfo(QuarkType::Unknown, expr->location);
    }
}

TypeInfo SemanticAnalyzer::analyzeUnary(UnaryExprAST* expr) {
    TypeInfo operandType = analyzeExpr(expr->operand.get());
    
    switch (expr->op) {
        case '-':
            if (!IntegerTypeUtils::isNumericType(operandType.type)) {
                error("unary minus requires numeric operand", expr->location, "E136");
            }
            return operandType;
            
        case '!':
            if (operandType.type != QuarkType::Boolean && operandType.type != QuarkType::Int) {
                error("logical not requires boolean or integer operand", expr->location, "E137");
            }
            return TypeInfo(QuarkType::Boolean, expr->location);
            
        default:
            return operandType;
    }
}

TypeInfo SemanticAnalyzer::analyzeStructLiteral(StructLiteralExpr* expr) {
    Symbol* structSym = symbolTable_.lookup(expr->structName);
    if (!structSym || structSym->kind != Symbol::Kind::Struct) {
        error("unknown struct '" + expr->structName + "'", expr->location, "E132", expr->structName.size());
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    auto fields = getStructFields(expr->structName);
    std::unordered_set<std::string> providedFields;
    
    for (const auto& fieldVal : expr->fieldValues) {
        if (providedFields.count(fieldVal.first)) {
            error("duplicate field '" + fieldVal.first + "' in struct literal", 
                  expr->location, "E138", fieldVal.first.size());
            continue;
        }
        providedFields.insert(fieldVal.first);
        
        bool found = false;
        for (const auto& field : fields) {
            if (field.first == fieldVal.first) {
                found = true;
                TypeInfo valueType = analyzeExpr(fieldVal.second.get());
                TypeInfo fieldType = resolveType(field.second);
                
                if (!isTypeCompatible(fieldType, valueType)) {
                    error("field '" + fieldVal.first + "' type mismatch: expected '" + 
                          field.second + "', got '" + typeToString(valueType) + "'",
                          fieldVal.second->location, "E139");
                }
                break;
            }
        }
        
        if (!found) {
            error("struct '" + expr->structName + "' has no field named '" + fieldVal.first + "'",
                  expr->location, "E114", fieldVal.first.size());
        }
    }
    
    for (const auto& field : fields) {
        if (!providedFields.count(field.first)) {
            warning("field '" + field.first + "' not initialized in struct literal",
                    expr->location, "W003", field.first.size());
        }
    }
    
    return TypeInfo(QuarkType::Struct, expr->location, expr->structName);
}

TypeInfo SemanticAnalyzer::analyzeArrayLiteral(ArrayLiteralExpr* expr) {
    if (expr->elements.empty()) {
        return TypeInfo(QuarkType::Array, expr->location, "", QuarkType::Unknown);
    }
    
    TypeInfo firstElemType = analyzeExpr(expr->elements[0].get());
    
    for (size_t i = 1; i < expr->elements.size(); ++i) {
        TypeInfo elemType = analyzeExpr(expr->elements[i].get());
        if (!isTypeCompatible(firstElemType, elemType)) {
            error("array elements must have consistent types", 
                  expr->elements[i]->location, "E140");
        }
    }
    
    TypeInfo arrayType(QuarkType::Array, expr->location);
    arrayType.elementType = firstElemType.type;
    arrayType.structName = firstElemType.structName;
    arrayType.arraySize = expr->elements.size();
    return arrayType;
}

TypeInfo SemanticAnalyzer::analyzeCast(CastExpr* expr) {
    analyzeExpr(expr->operand.get());
    return resolveType(expr->targetTypeName);
}

TypeInfo SemanticAnalyzer::analyzeAddressOf(AddressOfExpr* expr) {
    TypeInfo operandType = analyzeExpr(expr->operand.get());
    
    TypeInfo ptrType(QuarkType::Pointer, expr->location);
    ptrType.elementType = operandType.type;
    ptrType.structName = operandType.structName;
    ptrType.pointerTypeName = typeToString(operandType) + "*";
    return ptrType;
}

TypeInfo SemanticAnalyzer::analyzeDereference(DereferenceExpr* expr) {
    TypeInfo ptrType = analyzeExpr(expr->operand.get());
    
    if (ptrType.type != QuarkType::Pointer) {
        error("cannot dereference non-pointer type", expr->location, "E119");
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
    
    TypeInfo derefType;
    derefType.type = ptrType.elementType;
    derefType.structName = ptrType.structName;
    derefType.location = expr->location;
    return derefType;
}

bool SemanticAnalyzer::isTypeCompatible(const TypeInfo& target, const TypeInfo& source) {
    if (target.type == QuarkType::Unknown || source.type == QuarkType::Unknown) {
        return true;
    }
    
    if (target.type == source.type) {
        if (target.type == QuarkType::Struct) {
            return target.structName == source.structName;
        }
        if (target.type == QuarkType::Array) {
            if (source.elementType == QuarkType::Unknown) {
                return true;
            }
            return target.elementType == source.elementType;
        }
        return true;
    }
    
    return canImplicitlyConvert(source, target);
}

bool SemanticAnalyzer::canImplicitlyConvert(const TypeInfo& from, const TypeInfo& to) {
    if (from.type == QuarkType::Int && to.type == QuarkType::Double) return true;
    if (from.type == QuarkType::Int && to.type == QuarkType::Float) return true;
    if (from.type == QuarkType::Float && to.type == QuarkType::Double) return true;
    if (from.type == QuarkType::Double && to.type == QuarkType::Float) return true;
    
    if (from.type == QuarkType::Int && to.type == QuarkType::Boolean) return true;
    if (from.type == QuarkType::Boolean && to.type == QuarkType::Int) return true;
    
    return false;
}

TypeInfo SemanticAnalyzer::resolveType(const std::string& typeName) {
    if (typeName.empty() || typeName == "var") {
        return TypeInfo(QuarkType::Unknown);
    }
    
    if (typeName == "int") return TypeInfo(QuarkType::Int);
    if (typeName == "float") return TypeInfo(QuarkType::Float);
    if (typeName == "double") return TypeInfo(QuarkType::Double);
    if (typeName == "str") return TypeInfo(QuarkType::String);
    if (typeName == "bool") return TypeInfo(QuarkType::Boolean);
    if (typeName == "void") return TypeInfo(QuarkType::Void);
    
    if (typeName.size() > 2 && typeName.substr(typeName.size() - 2) == "[]") {
        std::string elemTypeName = typeName.substr(0, typeName.size() - 2);
        TypeInfo elemType = resolveType(elemTypeName);
        TypeInfo arrayType(QuarkType::Array);
        arrayType.elementType = elemType.type;
        arrayType.structName = elemType.structName;
        return arrayType;
    }
    
    if (!typeName.empty() && typeName.back() == '*') {
        std::string pointeeTypeName = typeName.substr(0, typeName.size() - 1);
        TypeInfo pointeeType = resolveType(pointeeTypeName);
        TypeInfo ptrType(QuarkType::Pointer);
        ptrType.elementType = pointeeType.type;
        ptrType.structName = pointeeType.structName;
        ptrType.pointerTypeName = typeName;
        return ptrType;
    }
    
    Symbol* structSym = symbolTable_.lookup(typeName);
    if (structSym && structSym->kind == Symbol::Kind::Struct) {
        return TypeInfo(QuarkType::Struct, {}, typeName);
    }
    
    return TypeInfo(QuarkType::Unknown);
}

std::string SemanticAnalyzer::typeToString(const TypeInfo& type) {
    switch (type.type) {
        case QuarkType::Int: return "int";
        case QuarkType::Float: return "float";
        case QuarkType::Double: return "double";
        case QuarkType::String: return "str";
        case QuarkType::Boolean: return "bool";
        case QuarkType::Void: return "void";
        case QuarkType::Struct: return type.structName;
        case QuarkType::Array: {
            std::string elemStr;
            switch (type.elementType) {
                case QuarkType::Int: elemStr = "int"; break;
                case QuarkType::Float: elemStr = "float"; break;
                case QuarkType::Double: elemStr = "double"; break;
                case QuarkType::String: elemStr = "str"; break;
                case QuarkType::Boolean: elemStr = "bool"; break;
                case QuarkType::Struct: elemStr = type.structName; break;
                default: elemStr = "unknown"; break;
            }
            return elemStr + "[]";
        }
        case QuarkType::Pointer: return type.pointerTypeName.empty() ? "ptr" : type.pointerTypeName;
        default: return "unknown";
    }
}

std::vector<std::pair<std::string, std::string>> SemanticAnalyzer::getStructFields(const std::string& structName) {
    std::vector<std::pair<std::string, std::string>> allFields;
    
    std::function<void(const std::string&)> collectFields = [&](const std::string& name) {
        Symbol* sym = symbolTable_.lookup(name);
        if (!sym || sym->kind != Symbol::Kind::Struct) return;
        
        if (!sym->parentStruct.empty()) {
            collectFields(sym->parentStruct);
        }
        
        for (const auto& field : sym->structFields) {
            allFields.push_back(field);
        }
    };
    
    collectFields(structName);
    return allFields;
}

Symbol* SemanticAnalyzer::findMethod(const std::string& structName, const std::string& methodName) {
    std::string fullName = structName + "::" + methodName;
    Symbol* method = symbolTable_.lookup(fullName);
    if (method) return method;
    
    Symbol* structSym = symbolTable_.lookup(structName);
    if (structSym && !structSym->parentStruct.empty()) {
        return findMethod(structSym->parentStruct, methodName);
    }
    
    return nullptr;
}

void SemanticAnalyzer::error(const std::string& message, const SourceLocation& loc,
                             const std::string& code, int span) {
    errors_.push_back({message, loc, code, span, false});
}

void SemanticAnalyzer::warning(const std::string& message, const SourceLocation& loc,
                               const std::string& code, int span) {
    warnings_.push_back({message, loc, code, span, true});
    errors_.push_back({message, loc, code, span, true});
}
