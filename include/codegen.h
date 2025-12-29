#pragma once
#include "parser.h"
#include "codegen_types.h"
#include "external_functions.h"
#include "builtin_functions.h"
#include "expression_codegen.h"
#include "statement_codegen.h"
#include <llvm-c/Core.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/IR/PassManager.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

// Forward declaration
class CompilationProgress;

class CodeGen
{
public:
    CodeGen(bool verbose = false, bool optimize = false, int optimizationLevel = 2, bool freestanding = false,
        std::vector<std::string> additionalLibraries = {},
        std::vector<std::string> additionalLibraryPaths = {},
        bool showProgress = false);
    ~CodeGen();
    void generate(ProgramAST *program, const std::string &outFile);
    // Emit the in-memory LLVM module to a native executable (platform-specific).
    // Returns true on success, false on failure (errors are printed to stderr).
    bool emitExecutable(const std::string &outPath);

private:
    // LLVM context and module
    LLVMContextRef ctx_ = nullptr;
    LLVMModuleRef module_ = nullptr;
    LLVMBuilderRef builder_ = nullptr;
    
    // LLVM types
    LLVMTypeRef int32_t_ = nullptr;
    LLVMTypeRef int8ptr_t_ = nullptr;
    LLVMTypeRef fileptr_t_ = nullptr;
    LLVMTypeRef bool_t_ = nullptr;
    LLVMTypeRef float_t_ = nullptr;
    LLVMTypeRef double_t_ = nullptr;
    
    // Code generation modules
    std::unique_ptr<ExternalFunctions> externalFunctions_;
    std::unique_ptr<BuiltinFunctions> builtinFunctions_;
    std::unique_ptr<ExpressionCodeGen> expressionCodeGen_;
    std::unique_ptr<StatementCodeGen> statementCodeGen_;
    
    // Global symbol tables
    std::unordered_map<std::string, LLVMValueRef> g_function_map_;
    std::unordered_map<std::string, LLVMValueRef> g_named_values_;
    std::unordered_map<std::string, double> g_const_values_;
    std::unordered_map<std::string, std::vector<LLVMTypeRef>> g_function_param_types_;
    std::unordered_map<std::string, LLVMTypeRef> g_named_types_;
    std::unordered_map<std::string, StructDefStmt*> g_struct_defs_;
    std::unordered_map<std::string, LLVMTypeRef> g_struct_types_;
    std::unordered_map<std::string, bool> g_variadic_functions_; // Track which functions are variadic
    
    bool verbose_ = false;
    bool optimize_ = false;
    bool freestanding_ = false;
    bool showProgress_ = false;
    int optimizationLevel_ = 2;
    std::vector<std::string> additionalLibraries_;
    std::vector<std::string> additionalLibraryPaths_;
    std::unique_ptr<CompilationProgress> progress_;
    
    // Helper methods
    void initializeCodeGenModules();
    void declareFunctions(const std::vector<FunctionAST*>& functions);
    void generateMainFunction(ProgramAST* program, bool hasUserMain, bool hasTopLevelStmts);
    void runOptimizationPasses();
};
