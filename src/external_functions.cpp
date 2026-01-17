#include "../include/external_functions.h"

ExternalFunctions::ExternalFunctions(LLVMContextRef ctx, LLVMModuleRef module)
    : ctx_(ctx), module_(module)
{
    int32_t_ = LLVMInt32TypeInContext(ctx_);
    int8ptr_t_ = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);

#ifdef _WIN32
    fileptr_t_ = LLVMPointerType(LLVMStructCreateNamed(ctx_, "struct._iobuf"), 0);
#else
    fileptr_t_ = LLVMPointerType(LLVMStructCreateNamed(ctx_, "struct._IO_FILE"), 0);
#endif
    bool_t_ = LLVMInt1TypeInContext(ctx_);
}

LLVMValueRef ExternalFunctions::promoteForVarArg(LLVMBuilderRef builder, LLVMValueRef v)
{
    LLVMTypeRef ty = LLVMTypeOf(v);
    if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind)
    {
        unsigned w = LLVMGetIntTypeWidth(ty);
        if (w < 32)
        {
            return LLVMBuildSExt(builder, v, LLVMInt32TypeInContext(ctx_), "sext_i32");
        }
        if (w == 32)
        {
            return LLVMBuildSExt(builder, v, LLVMInt64TypeInContext(ctx_), "sext_i64");
        }
    }
    return v;
}
