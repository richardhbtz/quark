#pragma once
#include "parser.h"
#include <llvm-c/Core.h>
#include <string>
#include <optional>
#include <unordered_map>
#include <stdexcept>

enum class QuarkType {
    Unknown,
    Integer,
    String,
    Boolean,
    Void
};

struct TypeInfo {
    QuarkType type;
    SourceLocation location;
    
    TypeInfo(QuarkType t = QuarkType::Unknown, SourceLocation loc = {}) 
        : type(t), location(loc) {}
};

class CodeGenError : public std::runtime_error
{
public:
    SourceLocation location;
    CodeGenError(const std::string &msg, const SourceLocation &loc)
        : std::runtime_error(formatError(msg, loc)), location(loc) {}
        
private:
    static std::string formatError(const std::string &msg, const SourceLocation &loc) {
        return loc.filename + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": codegen error: " + msg;
    }
};

class CodeGen
{
public:
    CodeGen(bool verbose = false);
    ~CodeGen();
    void generate(ProgramAST *program, const std::string &outFile);

private:
    LLVMContextRef ctx_ = nullptr;
    LLVMModuleRef module_ = nullptr;
    LLVMBuilderRef builder_ = nullptr;
    LLVMTypeRef int32_t_ = nullptr;
    LLVMTypeRef int8ptr_t_ = nullptr;
    LLVMTypeRef fileptr_t_ = nullptr;
    LLVMTypeRef bool_t_ = nullptr; // LLVM i1 type for booleans

    LLVMValueRef declarePuts();
    LLVMValueRef declareSprintf();
    LLVMValueRef declareAcrtIobFunc();
    LLVMValueRef declareStrlen();
    LLVMValueRef declareStrcat();
    LLVMValueRef declareStrcpy();
    LLVMValueRef declareMalloc();
    LLVMValueRef declareFree();
    LLVMValueRef declareFgets();
    LLVMValueRef declareAtoi();
    LLVMValueRef genExpr(ExprAST *expr);
    LLVMValueRef genExprInt(ExprAST *expr); // emit an i32 value for runtime integer expressions
    LLVMValueRef genExprBool(ExprAST *expr); // emit an i1 value for boolean expressions
    void genStmt(StmtAST *stmt, LLVMValueRef putsFn);
    void collectFunctions(StmtAST *stmt, std::vector<FunctionAST*> &functions);
    std::optional<double> evalConst(ExprAST *expr);
    std::optional<std::string> evalConstString(ExprAST *expr);
    // Promote integers for varargs (e.g., Win64 ABI)
    LLVMValueRef promoteForVarArg(LLVMValueRef v);
    
    // Type checking methods
    TypeInfo inferType(ExprAST *expr);
    void checkTypeCompatibility(QuarkType expected, QuarkType actual, const SourceLocation &loc, const std::string &context);
    QuarkType getVariableType(const std::string &name);
    void declareVariable(const std::string &name, QuarkType type, const SourceLocation &loc);
    
    // Type tracking
    std::unordered_map<std::string, TypeInfo> variableTypes_;
    std::unordered_map<std::string, TypeInfo> functionTypes_;
    
    bool verbose_ = false;
};
