#pragma once

#include "parser.h"
#include "codegen_types.h"
#include "error_reporter.h"
#include "source_manager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>

struct Symbol {
    enum class Kind {
        Variable,
        Function,
        Struct,
        Parameter,
        Field
    };
    
    Kind kind;
    std::string name;
    std::string typeName;
    QuarkType resolvedType;
    SourceLocation declLocation;
    TypeInfo type; // Full type info including funcPtrInfo for function pointers
    
    bool isMutable = true;
    bool isInitialized = false;
    std::string structName;
    QuarkType elementType = QuarkType::Unknown;
    
    std::vector<std::pair<std::string, std::string>> functionParams;
    std::string returnType;
    bool isVariadic = false;
    bool isExtern = false;
    bool isMethod = false;
    
    std::vector<std::pair<std::string, std::string>> structFields;
    std::string parentStruct;
    std::vector<std::string> methodNames;
};

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}
    
    bool declare(const std::string& name, Symbol symbol);
    Symbol* lookup(const std::string& name);
    Symbol* lookupLocal(const std::string& name);
    Scope* parent() { return parent_; }
    
    std::unordered_map<std::string, Symbol>& symbols() { return symbols_; }
    const std::unordered_map<std::string, Symbol>& symbols() const { return symbols_; }
    
private:
    Scope* parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

class SymbolTable {
public:
    SymbolTable();
    
    void enterScope();
    void exitScope();
    
    bool declare(const std::string& name, Symbol symbol);
    Symbol* lookup(const std::string& name);
    Symbol* lookupLocal(const std::string& name);
    
    Scope* currentScope() { return currentScope_; }
    Scope* globalScope() { return globalScope_.get(); }
    
private:
    std::unique_ptr<Scope> globalScope_;
    Scope* currentScope_;
    std::vector<std::unique_ptr<Scope>> scopeStack_;
};

struct SemanticError {
    std::string message;
    SourceLocation location;
    std::string errorCode;
    int spanLength;
    bool isWarning;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer(ErrorReporter& errorReporter, SourceManager& sourceManager, 
                     const std::string& filename, bool verbose = false);
    
    bool analyze(ProgramAST* program);
    void registerBuiltinFunctions();
    
    const std::vector<SemanticError>& errors() const { return errors_; }
    const std::vector<SemanticError>& warnings() const { return warnings_; }
    
    SymbolTable& symbolTable() { return symbolTable_; }
    
private:
    void collectDeclarations(ProgramAST* program);
    void analyzeStatements(ProgramAST* program);
    
    void collectStructDef(StructDefStmt* stmt);
    void collectFunction(FunctionAST* func);
    void collectExternFunction(ExternFunctionAST* func);
    void collectExternVariable(ExternVarAST* var);
    void collectImplBlock(ImplStmt* impl);
    
    void analyzeStmt(StmtAST* stmt);
    void analyzeFunction(FunctionAST* func);
    void analyzeVarDecl(VarDeclStmt* stmt);
    void analyzeAssign(AssignStmtAST* stmt);
    void analyzeMemberAssign(MemberAssignStmt* stmt);
    void analyzeArrayAssign(ArrayAssignStmt* stmt);
    void analyzeDerefAssign(DerefAssignStmt* stmt);
    void analyzeIf(IfStmtAST* stmt);
    void analyzeWhile(WhileStmt* stmt);
    void analyzeFor(ForStmt* stmt);
    void analyzeReturn(ReturnStmt* stmt);
    void analyzeExprStmt(ExprStmtAST* stmt);
    void analyzeStructDef(StructDefStmt* stmt);
    void analyzeImplBlock(ImplStmt* stmt);
    void analyzeInclude(IncludeStmt* stmt);
    
    TypeInfo analyzeExpr(ExprAST* expr);
    TypeInfo analyzeCall(CallExprAST* expr);
    TypeInfo analyzeMethodCall(MethodCallExpr* expr);
    TypeInfo analyzeStaticCall(StaticCallExpr* expr);
    TypeInfo analyzeMemberAccess(MemberAccessExpr* expr);
    TypeInfo analyzeArrayAccess(ArrayAccessExpr* expr);
    TypeInfo analyzeBinary(BinaryExprAST* expr);
    TypeInfo analyzeUnary(UnaryExprAST* expr);
    TypeInfo analyzeStructLiteral(StructLiteralExpr* expr);
    TypeInfo analyzeArrayLiteral(ArrayLiteralExpr* expr);
    TypeInfo analyzeMapLiteral(MapLiteralExpr* expr);
    TypeInfo analyzeListLiteral(ListLiteralExpr* expr);
    TypeInfo analyzeCast(CastExpr* expr);
    TypeInfo analyzeAddressOf(AddressOfExpr* expr);
    TypeInfo analyzeDereference(DereferenceExpr* expr);
    
    bool isTypeCompatible(const TypeInfo& target, const TypeInfo& source);
    bool canImplicitlyConvert(const TypeInfo& from, const TypeInfo& to);
    TypeInfo resolveType(const std::string& typeName);
    std::string typeToString(const TypeInfo& type);
    
    std::vector<std::pair<std::string, std::string>> getStructFields(const std::string& structName);
    Symbol* findMethod(const std::string& structName, const std::string& methodName);
    
    void error(const std::string& message, const SourceLocation& loc, 
               const std::string& code = "", int span = 1);
    void warning(const std::string& message, const SourceLocation& loc,
                 const std::string& code = "", int span = 1);
    
    ErrorReporter& errorReporter_;
    SourceManager& sourceManager_;
    std::string filename_;
    std::string sourceCode_;
    bool verbose_;
    
    SymbolTable symbolTable_;
    std::vector<SemanticError> errors_;
    std::vector<SemanticError> warnings_;
    
    std::string currentFunctionName_;
    std::string currentFunctionReturnType_;
    std::string currentStructName_;
    bool inLoop_ = false;
    bool hasReturn_ = false;
    
    std::unordered_set<std::string> includedFiles_;
};
