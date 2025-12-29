#pragma once
#include "parser.h"
#include "codegen_types.h"
#include "external_functions.h"
#include "expression_codegen.h"
#include <llvm-c/Core.h>
#include <string>
#include <unordered_map>
#include <vector>

class StatementCodeGen
{
public:
    StatementCodeGen(LLVMContextRef ctx, LLVMModuleRef module, LLVMBuilderRef builder,
                     ExternalFunctions* externalFunctions, ExpressionCodeGen* expressionCodeGen, 
                     bool verbose = false);
    ~StatementCodeGen() = default;
    
    // Statement generation methods
    void genStmt(StmtAST *stmt, LLVMValueRef putsFn);
    void collectFunctions(StmtAST *stmt, std::vector<FunctionAST*> &functions);
    void collectExternFunctions(StmtAST *stmt, std::vector<ExternFunctionAST*> &externFunctions);
    void collectStructDefs(StmtAST *stmt, std::vector<StructDefStmt*> &structDefs);
    void declareExternFunction(ExternFunctionAST* externFunc);
    void processStructDef(StructDefStmt* structDef); // Public wrapper for genStructDefStmt
    // Ensure extern C opaque structs are registered before function declarations
    void predeclareExternStructs(StmtAST* stmt);
    
    // Symbol table access
    void setGlobalSymbolTables(std::unordered_map<std::string, LLVMValueRef>* functionMap,
                               std::unordered_map<std::string, LLVMValueRef>* namedValues,
                               std::unordered_map<std::string, double>* constValues,
                               std::unordered_map<std::string, std::vector<LLVMTypeRef>>* functionParamTypes,
                               std::unordered_map<std::string, LLVMTypeRef>* namedTypes,
                               std::unordered_map<std::string, StructDefStmt*>* structDefs = nullptr,
                               std::unordered_map<std::string, LLVMTypeRef>* structTypes = nullptr,
                               std::unordered_map<std::string, bool>* variadicFunctions = nullptr);

private:
    LLVMContextRef ctx_;
    LLVMModuleRef module_;
    LLVMBuilderRef builder_;
    ExternalFunctions* externalFunctions_;
    ExpressionCodeGen* expressionCodeGen_;
    bool verbose_;
    
    // LLVM types
    LLVMTypeRef int32_t_;
    LLVMTypeRef int8ptr_t_;
    LLVMTypeRef bool_t_;
    LLVMTypeRef float_t_;
    LLVMTypeRef double_t_;
    
    // Global symbol tables (shared with main CodeGen)
    std::unordered_map<std::string, LLVMValueRef>* g_function_map_;
    std::unordered_map<std::string, LLVMValueRef>* g_named_values_;
    std::unordered_map<std::string, double>* g_const_values_;
    std::unordered_map<std::string, std::vector<LLVMTypeRef>>* g_function_param_types_;
    std::unordered_map<std::string, LLVMTypeRef>* g_named_types_;
    std::unordered_map<std::string, StructDefStmt*>* g_struct_defs_;
    std::unordered_map<std::string, LLVMTypeRef>* g_struct_types_;
    std::unordered_map<std::string, bool>* g_variadic_functions_; // Track which functions are variadic
    
    // Current function context for return type handling
    std::string currentFunctionName_;
    std::string currentFunctionReturnType_;

    // Loop context for break/continue
    struct LoopContext { LLVMBasicBlockRef continueTarget; LLVMBasicBlockRef breakTarget; };
    std::vector<LoopContext> loopStack_;
    
    // Helper methods for specific statement types
    void genVarDeclStmt(VarDeclStmt* vdecl);
    void genAssignStmt(AssignStmtAST* assign);
    void genExprStmt(ExprStmtAST* exprStmt, LLVMValueRef putsFn);
    void genFunctionStmt(FunctionAST* func, LLVMValueRef putsFn);
    void genIncludeStmt(IncludeStmt* include, LLVMValueRef putsFn);
    void genIfStmt(IfStmtAST* ifstmt, LLVMValueRef putsFn);
    void genForStmt(ForStmt* forstmt, LLVMValueRef putsFn);
    void genWhileStmt(WhileStmt* whilestmt, LLVMValueRef putsFn);
    void genReturnStmt(ReturnStmt* retstmt);
    void genBreakStmt();
    void genContinueStmt();
    void createStructType(StructDefStmt* structDef);  // Create struct type without method bodies
    void genStructDefStmt(StructDefStmt* structDef);
    void genImplStmt(ImplStmt* impl, LLVMValueRef putsFn);
    void genMemberAssignStmt(MemberAssignStmt* memberAssign);
    void genDerefAssignStmt(DerefAssignStmt* derefAssign);
    void genArrayAssignStmt(ArrayAssignStmt* arrayAssign);
    void genMatchStmt(MatchStmt* matchStmt);
};
