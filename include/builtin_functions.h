#pragma once
#include <llvm-c/Core.h>
#include <unordered_map>
#include <string>
#include <functional>
#include <memory>
#include <vector>

// Forward declarations
class ExpressionCodeGen;

// Built-in function signature
struct BuiltinFunction {
    std::string name;
    LLVMTypeRef returnType;
    std::vector<LLVMTypeRef> paramTypes;
    bool isVariadic;
    std::function<LLVMValueRef(LLVMBuilderRef, const std::vector<LLVMValueRef>&)> implementation;
    
    BuiltinFunction(const std::string& n, LLVMTypeRef ret, std::vector<LLVMTypeRef> params, 
                    bool variadic, std::function<LLVMValueRef(LLVMBuilderRef, const std::vector<LLVMValueRef>&)> impl)
        : name(n), returnType(ret), paramTypes(std::move(params)), isVariadic(variadic), implementation(std::move(impl)) {}
};

class ExpressionCodeGen; // forward declaration

class BuiltinFunctions {
public:
    BuiltinFunctions(LLVMContextRef ctx, LLVMModuleRef module, LLVMBuilderRef builder);
    ~BuiltinFunctions() = default;
    
    // Register all built-in functions
    void registerAllBuiltins();
    
    // Register specific categories of built-in functions
    void registerStringFunctions();
    void registerArrayFunctions();
    void registerMathFunctions();
    void registerMemoryFunctions();
    // Register formatting helpers
    void registerFormatFunctions();
    
    // Check if a function is a built-in
    bool isBuiltin(const std::string& functionName) const;
    
    // Get built-in function information
    const BuiltinFunction* getBuiltin(const std::string& functionName) const;
    
    // Generate call to built-in function
    LLVMValueRef generateBuiltinCall(const std::string& functionName, const std::vector<LLVMValueRef>& args);
    
    // Register built-in functions with the global function map
    void registerWithGlobalMap(std::unordered_map<std::string, LLVMValueRef>* functionMap,
                               std::unordered_map<std::string, std::vector<LLVMTypeRef>>* functionParamTypes,
                               std::unordered_map<std::string, bool>* variadicFunctions = nullptr);

    // Register built-in function return types with an ExpressionCodeGen's functionTypes_
    void registerTypesWithExpressionCodeGen(class ExpressionCodeGen* exprGen);

private:
    LLVMContextRef ctx_;
    LLVMModuleRef module_;
    LLVMBuilderRef builder_;
    
    // LLVM types
    LLVMTypeRef int32_t_;
    LLVMTypeRef int8ptr_t_;  // string type
    LLVMTypeRef bool_t_;
    LLVMTypeRef void_t_;
    LLVMTypeRef float_t_;
    LLVMTypeRef double_t_;
    
    // Registry of built-in functions
    std::unordered_map<std::string, std::unique_ptr<BuiltinFunction>> builtins_;
    
    // Helper methods for common string operations
    LLVMValueRef createStringLiteral(const std::string& str);
    LLVMValueRef allocateString(LLVMValueRef size);
    // Helper conversions
    LLVMValueRef intToString(LLVMValueRef intVal);
    LLVMValueRef boolToString(LLVMValueRef boolVal);
    LLVMValueRef floatToString(LLVMValueRef floatVal);
    LLVMValueRef doubleToString(LLVMValueRef doubleVal);
    LLVMValueRef formatString(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    
    // String function implementations
    LLVMValueRef stringSlice(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringConcat(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringFind(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringReplace(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringSplit(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringLength(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringStartsWith(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    LLVMValueRef stringEndsWith(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args);
    
    // Register a built-in function
    void registerBuiltin(const std::string& name, LLVMTypeRef returnType, 
                        std::vector<LLVMTypeRef> paramTypes, bool isVariadic,
                        std::function<LLVMValueRef(LLVMBuilderRef, const std::vector<LLVMValueRef>&)> implementation);
                        
    // Declare C library functions needed for string operations
    void declareCLibraryFunctions();
    LLVMValueRef malloc_fn_;
    LLVMValueRef free_fn_;
    LLVMValueRef strlen_fn_;
    LLVMValueRef strcpy_fn_;
    LLVMValueRef strcat_fn_;
    LLVMValueRef strncpy_fn_;
    LLVMValueRef strstr_fn_;
    LLVMValueRef memcpy_fn_;
    LLVMValueRef memset_fn_;
    LLVMValueRef snprintf_fn_;
    LLVMValueRef printf_fn_;
    LLVMValueRef gets_s_fn_;
    LLVMValueRef strchr_fn_;
    LLVMValueRef fflush_fn_;
    LLVMValueRef atoi_fn_;
    LLVMValueRef strncmp_fn_;
};
