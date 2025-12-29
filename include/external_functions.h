#pragma once
#include <llvm-c/Core.h>

class ExternalFunctions
{
public:
    ExternalFunctions(LLVMContextRef ctx, LLVMModuleRef module);
    ~ExternalFunctions() = default;
    
    // Minimal C library function declarations - only for compiler internals
    // All user-facing functions should come from standard library (cong.k)
    
    // Utility functions
    LLVMValueRef promoteForVarArg(LLVMBuilderRef builder, LLVMValueRef v);
    
private:
    LLVMContextRef ctx_;
    LLVMModuleRef module_;
    LLVMTypeRef int32_t_;
    LLVMTypeRef int8ptr_t_;
    LLVMTypeRef fileptr_t_;
    LLVMTypeRef bool_t_;
};
