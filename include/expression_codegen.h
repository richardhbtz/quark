#pragma once
#include "parser.h"
#include "codegen_types.h"
#include "external_functions.h"
#include "builtin_functions.h"
#include <llvm-c/Core.h>
#include <string>
#include <optional>
#include <unordered_map>

class ExpressionCodeGen
{
public:
    ExpressionCodeGen(LLVMContextRef ctx, LLVMModuleRef module, LLVMBuilderRef builder, 
                      ExternalFunctions* externalFunctions, bool verbose = false);
    ~ExpressionCodeGen() = default;
    
    // Set builtin functions reference
    void setBuiltinFunctions(BuiltinFunctions* builtinFunctions);
    
    // Expression generation methods
    LLVMValueRef genExpr(ExprAST *expr);
    LLVMValueRef genExprInt(ExprAST *expr); // emit an i32 value for runtime integer expressions
    LLVMValueRef genExprIntWithType(ExprAST *expr, LLVMTypeRef targetType); // emit integer with specific type
    LLVMValueRef genExprBool(ExprAST *expr); // emit an i1 value for boolean expressions
    
    // Constant evaluation methods
    std::optional<double> evalConst(ExprAST *expr);
    std::optional<std::string> evalConstString(ExprAST *expr);
    
    // Type inference and checking methods
    TypeInfo inferType(ExprAST *expr);
    void checkTypeCompatibility(QuarkType expected, QuarkType actual, const SourceLocation &loc, const std::string &context);
    QuarkType getVariableType(const std::string &name);
    void declareVariable(const std::string &name, QuarkType type, const SourceLocation &loc, const std::string &structName = "", const std::string &pointerType = "");
    void declareVariable(const std::string &name, QuarkType type, const SourceLocation &loc, QuarkType elementType, size_t arraySize);
    void declareFunctionType(const std::string &name, QuarkType returnType, const SourceLocation &loc, const std::string &structName = "");
    
    // Symbol table access
    void setGlobalSymbolTables(std::unordered_map<std::string, LLVMValueRef>* functionMap,
                               std::unordered_map<std::string, LLVMValueRef>* namedValues,
                               std::unordered_map<std::string, double>* constValues,
                               std::unordered_map<std::string, std::vector<LLVMTypeRef>>* functionParamTypes,
                               std::unordered_map<std::string, LLVMTypeRef>* namedTypes,
                               std::unordered_map<std::string, StructDefStmt*>* structDefs = nullptr,
                               std::unordered_map<std::string, LLVMTypeRef>* structTypes = nullptr,
                               std::unordered_map<std::string, bool>* variadicFunctions = nullptr);

    // Parameter tracking (to distinguish parameters from allocas)
    void setFunctionParameters(const std::unordered_map<std::string, bool>& functionParams);
    void clearFunctionParameters();
    
    // Struct field access in methods
    void registerStructField(const std::string& fieldName, const std::string& structName, const std::string& fieldType);
    void clearStructFields();
    
    // Struct expression generation
    LLVMValueRef genStructLiteral(StructLiteralExpr *structLiteral);
    LLVMValueRef genMemberAccess(MemberAccessExpr *memberAccess);
    LLVMValueRef genMethodCall(MethodCallExpr *methodCall);
    LLVMValueRef genStaticCall(StaticCallExpr *staticCall);
    
    // Pointer and range expression generation
    LLVMValueRef genAddressOf(AddressOfExpr *addrOf);
    LLVMValueRef genDereference(DereferenceExpr *deref);
    LLVMValueRef genRange(RangeExpr *range);
    
    // Array expression generation
    LLVMValueRef genArrayLiteral(ArrayLiteralExpr *arrayLiteral);
    LLVMValueRef genArrayAccess(ArrayAccessExpr *arrayAccess);
    
    // Helper methods for type mapping (made public for use in statement codegen)
    LLVMTypeRef quarkTypeToLLVMType(QuarkType type);
    LLVMTypeRef mapPointerType(const std::string& typeName);

private:
    LLVMContextRef ctx_;
    LLVMModuleRef module_;
    LLVMBuilderRef builder_;
    ExternalFunctions* externalFunctions_;
    BuiltinFunctions* builtinFunctions_;
    bool verbose_;

    // LLVM types
    LLVMTypeRef int32_t_;    // int (32-bit signed integer)
    LLVMTypeRef int8ptr_t_;  // str (i8*)
    LLVMTypeRef fileptr_t_;  // FILE*
    LLVMTypeRef bool_t_;     // bool
    LLVMTypeRef float_t_;    // float (32-bit floating-point)
    LLVMTypeRef double_t_;   // double (64-bit floating-point)
    
    // Type tracking
    std::unordered_map<std::string, TypeInfo> variableTypes_;
    std::unordered_map<std::string, TypeInfo> functionTypes_;
    
    // Parameter tracking (true if variable is a direct function parameter, not an alloca)
    std::unordered_map<std::string, bool> function_params_;
    
    // Struct field tracking for method context
    std::unordered_map<std::string, std::pair<std::string, std::string>> struct_fields_; // fieldName -> (structName, fieldType)
    
    // Global symbol tables (shared with main CodeGen)
    std::unordered_map<std::string, LLVMValueRef>* g_function_map_;
    std::unordered_map<std::string, LLVMValueRef>* g_named_values_;
    std::unordered_map<std::string, double>* g_const_values_;
    std::unordered_map<std::string, std::vector<LLVMTypeRef>>* g_function_param_types_;
    std::unordered_map<std::string, LLVMTypeRef>* g_named_types_;
    std::unordered_map<std::string, StructDefStmt*>* g_struct_defs_;
    std::unordered_map<std::string, LLVMTypeRef>* g_struct_types_;
    std::unordered_map<std::string, bool>* g_variadic_functions_; // Track which functions are variadic

    // Helper methods for type mapping (quarkTypeToLLVMType moved to public section)
    QuarkType llvmTypeToQuarkType(LLVMTypeRef type);
    LLVMTypeRef stringToLLVMType(const std::string& typeName);
    bool isIntegerQuarkType(QuarkType type);
    bool isSignedIntegerType(QuarkType type);
    unsigned getIntegerBitWidth(QuarkType type);
    bool isLLVMIntegerType(LLVMTypeRef type);
};
