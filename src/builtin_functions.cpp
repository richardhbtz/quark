#include "../include/builtin_functions.h"
#include "../include/codegen_types.h"
#include "../include/expression_codegen.h"
#include <cstring>
#include <cstdlib>

BuiltinFunctions::BuiltinFunctions(LLVMContextRef ctx, LLVMModuleRef module, LLVMBuilderRef builder)
    : ctx_(ctx), module_(module), builder_(builder) {
    // Initialize LLVM types
    int32_t_ = LLVMInt32TypeInContext(ctx_);
    int8ptr_t_ = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
    bool_t_ = LLVMInt1TypeInContext(ctx_);
    void_t_ = LLVMVoidTypeInContext(ctx_);
    float_t_ = LLVMFloatTypeInContext(ctx_);
    double_t_ = LLVMDoubleTypeInContext(ctx_);
    
    // Declare C library functions
    declareCLibraryFunctions();
}

void BuiltinFunctions::registerAllBuiltins() {
    registerStringFunctions();
    registerArrayFunctions();
    registerMathFunctions();
    registerFormatFunctions();
    // sleep(ms: int) -> void (cross-platform)
    registerBuiltin("sleep", void_t_, {int32_t_}, /*isVariadic*/ false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) -> LLVMValueRef {
            LLVMValueRef ms = args.empty() ? LLVMConstInt(int32_t_, 0, 0) : args[0];

#if defined(_WIN32)
            // Win32 Sleep(DWORD ms)
            LLVMValueRef sleepFn = LLVMGetNamedFunction(module_, "Sleep");
            if (!sleepFn) {
                LLVMTypeRef argTys[] = { int32_t_ };
                LLVMTypeRef fnTy = LLVMFunctionType(void_t_, argTys, 1, 0);
                sleepFn = LLVMAddFunction(module_, "Sleep", fnTy);
            }
            LLVMValueRef callArgs[] = { ms };
            LLVMBuildCall2(builder_, LLVMGlobalGetValueType(sleepFn), sleepFn, callArgs, 1, "");
#else
            // POSIX usleep(usec)
            // Convert ms -> usec (ms * 1000)
            LLVMValueRef thousand = LLVMConstInt(int32_t_, 1000, 0);
            LLVMValueRef usec = LLVMBuildMul(builder_, ms, thousand, "ms_to_usec");

            LLVMValueRef usleepFn = LLVMGetNamedFunction(module_, "usleep");
            if (!usleepFn) {
                LLVMTypeRef argTys[] = { int32_t_ };
                LLVMTypeRef fnTy = LLVMFunctionType(int32_t_, argTys, 1, 0);
                usleepFn = LLVMAddFunction(module_, "usleep", fnTy);
            }
            LLVMValueRef callArgs[] = { usec };
            LLVMBuildCall2(builder_, LLVMGlobalGetValueType(usleepFn), usleepFn, callArgs, 1, "");
#endif
            return nullptr;
        }
    );
        registerBuiltin("to_string", int8ptr_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) -> LLVMValueRef {
                        if (args.size() != 1) {
                return createStringLiteral("");
            }
            LLVMValueRef fmt = createStringLiteral("{}");
            std::vector<LLVMValueRef> fargs;
            fargs.push_back(fmt);
            fargs.push_back(args[0]);
            return formatString(builder, fargs);
        }
    );

    registerBuiltin("to_int", int32_t_, { }, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) -> LLVMValueRef {
            if (args.size() != 1) {
                return LLVMConstInt(int32_t_, 0, 0);
            }
            LLVMValueRef v = args[0];
            LLVMTypeRef ty = LLVMTypeOf(v);
            if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) {
                unsigned w = LLVMGetIntTypeWidth(ty);
                if (w == 1) {
                    // bool -> int (0/1)
                    return LLVMBuildZExt(builder_, v, int32_t_, "bool_to_int");
                }
                if (w == 32) return v;
                                if (w > 32) return LLVMBuildTrunc(builder_, v, int32_t_, "trunc_i32");
                return LLVMBuildSExt(builder_, v, int32_t_, "sext_i32");
            }
            if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind) {
                                LLVMValueRef asStr = LLVMBuildPointerCast(builder_, v, int8ptr_t_, "to_cstr");
                LLVMValueRef callArgs[] = { asStr };
                return LLVMBuildCall2(builder_, LLVMGlobalGetValueType(atoi_fn_), atoi_fn_, callArgs, 1, "atoi_call");
            }
            if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) {
                return LLVMBuildFPToSI(builder_, v, int32_t_, "fptosi_i32");
            }
            if (LLVMGetTypeKind(ty) == LLVMDoubleTypeKind) {
                return LLVMBuildFPToSI(builder_, v, int32_t_, "fptosi_i32");
            }
            // Default 0
            return LLVMConstInt(int32_t_, 0, 0);
        }
    );
        registerBuiltin("print", void_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) -> LLVMValueRef {
            // If no args: print newline
            if (args.empty()) {
                LLVMValueRef nl = createStringLiteral("\n");
                LLVMValueRef fmt = createStringLiteral("%s");
                LLVMValueRef callArgs[] = { fmt, nl };
                LLVMBuildCall2(builder_, LLVMGlobalGetValueType(printf_fn_), printf_fn_, callArgs, 2, "");
                return nullptr;
            }
            
                        if (args.size() == 1) {
                LLVMValueRef arg = args[0];
                LLVMTypeRef ty = LLVMTypeOf(arg);
                LLVMValueRef asStr = nullptr;
                bool allocated = false;
                
                if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind) {
                                        asStr = LLVMBuildPointerCast(builder_, arg, int8ptr_t_, "to_str");
                } else if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) {
                    unsigned w = LLVMGetIntTypeWidth(ty);
                    if (w == 1) {
                        asStr = boolToString(arg);
                        allocated = true;
                    } else {
                                                LLVMValueRef i32v = (w == 32) ? arg : LLVMBuildSExt(builder_, arg, int32_t_, "to_i32");
                        asStr = intToString(i32v);
                        allocated = true;
                    }
                } else if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) {
                    asStr = floatToString(arg);
                    allocated = true;
                } else if (LLVMGetTypeKind(ty) == LLVMDoubleTypeKind) {
                    asStr = doubleToString(arg);
                    allocated = true;
                } else {
                    // Fallback: cast to i8*
                    asStr = LLVMBuildPointerCast(builder_, arg, int8ptr_t_, "to_str_any");
                }
                
                                LLVMValueRef fmt = createStringLiteral("%s\n");
                LLVMValueRef callArgs[] = { fmt, asStr };
                LLVMBuildCall2(builder_, LLVMGlobalGetValueType(printf_fn_), printf_fn_, callArgs, 2, "");
                if (allocated) {
                    LLVMBuildCall2(builder_, LLVMGlobalGetValueType(free_fn_), free_fn_, &asStr, 1, "");
                }
                return nullptr;
            } else {
                                LLVMValueRef outStr = formatString(builder_, args);
                LLVMValueRef fmt = createStringLiteral("%s\n");
                LLVMValueRef callArgs[] = { fmt, outStr };
                LLVMBuildCall2(builder_, LLVMGlobalGetValueType(printf_fn_), printf_fn_, callArgs, 2, "");
                LLVMBuildCall2(builder_, LLVMGlobalGetValueType(free_fn_), free_fn_, &outStr, 1, "");
                return nullptr;
            }
        }
    );

            registerBuiltin("readline", int8ptr_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) -> LLVMValueRef {
                        if (!args.empty()) {
                LLVMValueRef fmt = createStringLiteral("%s");
                LLVMValueRef callArgs[] = { fmt, args[0] };
                LLVMBuildCall2(builder_, LLVMGlobalGetValueType(printf_fn_), printf_fn_, callArgs, 2, "");
                                LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
                LLVMBuildCall2(builder_, LLVMGlobalGetValueType(fflush_fn_), fflush_fn_, &nullPtr, 1, "");
            }
                        LLVMValueRef capacity = LLVMConstInt(int32_t_, 4096, 0);
            LLVMValueRef buffer = allocateString(capacity);
                        LLVMValueRef getsArgs[] = { buffer, capacity };
            LLVMValueRef res = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(gets_s_fn_), gets_s_fn_, getsArgs, 2, "readline_res");

                        LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
            LLVMValueRef isNull = LLVMBuildICmp(builder_, LLVMIntEQ, res, nullPtr, "rd_isnull");

            LLVMBasicBlockRef curBB = LLVMGetInsertBlock(builder_);
            LLVMValueRef func = LLVMGetBasicBlockParent(curBB);
            LLVMBasicBlockRef onNullBB = LLVMAppendBasicBlockInContext(ctx_, func, "rd_on_null");
            LLVMBasicBlockRef onOkBB = LLVMAppendBasicBlockInContext(ctx_, func, "rd_on_ok");
            LLVMBasicBlockRef afterBB = LLVMAppendBasicBlockInContext(ctx_, func, "rd_after");

            LLVMValueRef retAlloca = LLVMBuildAlloca(builder_, int8ptr_t_, "rd_ret");
            LLVMBuildCondBr(builder_, isNull, onNullBB, onOkBB);

                        LLVMPositionBuilderAtEnd(builder_, onNullBB);
            LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
                        LLVMBuildCall2(builder_, LLVMGlobalGetValueType(free_fn_), free_fn_, &buffer, 1, "");
            LLVMValueRef empty = allocateString(one);
            LLVMValueRef zeroIdx = LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef rd_idx0[] = { zeroIdx };
            LLVMValueRef emptyPtr = LLVMBuildGEP2(builder_, LLVMInt8TypeInContext(ctx_), empty, rd_idx0, 1, "rd_empty_ptr");
            LLVMBuildStore(builder_, LLVMConstInt(LLVMInt8TypeInContext(ctx_), 0, 0), emptyPtr);
            LLVMBuildStore(builder_, empty, retAlloca);
            LLVMBuildBr(builder_, afterBB);

                        LLVMPositionBuilderAtEnd(builder_, onOkBB);
                        LLVMValueRef nlChar = LLVMConstInt(int32_t_, '\n', 0);
            LLVMValueRef strchrArgs[] = { buffer, nlChar };
            LLVMValueRef nlPtr = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strchr_fn_), strchr_fn_, strchrArgs, 2, "rd_nl_ptr");
            LLVMValueRef nullPtr2 = LLVMConstNull(int8ptr_t_);
            LLVMValueRef hasNl = LLVMBuildICmp(builder_, LLVMIntNE, nlPtr, nullPtr2, "rd_has_nl");

            LLVMBasicBlockRef trimBB = LLVMAppendBasicBlockInContext(ctx_, func, "rd_trim");
            LLVMBasicBlockRef noTrimBB = LLVMAppendBasicBlockInContext(ctx_, func, "rd_no_trim");
            LLVMBuildCondBr(builder_, hasNl, trimBB, noTrimBB);

            LLVMPositionBuilderAtEnd(builder_, trimBB);
            LLVMBuildStore(builder_, LLVMConstInt(LLVMInt8TypeInContext(ctx_), 0, 0), nlPtr);
            LLVMBuildBr(builder_, noTrimBB);

            LLVMPositionBuilderAtEnd(builder_, noTrimBB);
            LLVMBuildStore(builder_, buffer, retAlloca);
            LLVMBuildBr(builder_, afterBB);

                        LLVMPositionBuilderAtEnd(builder_, afterBB);
            LLVMValueRef finalRes = LLVMBuildLoad2(builder_, int8ptr_t_, retAlloca, "rd_final");
            return finalRes;
        }
    );
}

void BuiltinFunctions::registerStringFunctions() {
        registerBuiltin("str_concat", int8ptr_t_, {int8ptr_t_, int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringConcat(builder, args);
        });
        
        registerBuiltin("str_slice", int8ptr_t_, {int8ptr_t_, int32_t_, int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringSlice(builder, args);
        });
        
        registerBuiltin("str_find", bool_t_, {int8ptr_t_, int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringFind(builder, args);
        });
        
        registerBuiltin("str_replace", int8ptr_t_, {int8ptr_t_, int8ptr_t_, int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringReplace(builder, args);
        });
        
        registerBuiltin("str_split", LLVMPointerType(int8ptr_t_, 0), {int8ptr_t_, int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringSplit(builder, args);
        });
        
        registerBuiltin("str_len", int32_t_, {int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringLength(builder, args);
        });

        registerBuiltin("str_length", int32_t_, {int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringLength(builder, args);
        });

        registerBuiltin("str_starts_with", bool_t_, {int8ptr_t_, int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringStartsWith(builder, args);
        });

        registerBuiltin("str_ends_with", bool_t_, {int8ptr_t_, int8ptr_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return stringEndsWith(builder, args);
        });
}

void BuiltinFunctions::registerArrayFunctions() {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);

        registerBuiltin("array_length", int32_t_, {i8p}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            LLVMValueRef arrDataPtr = args[0];
            LLVMValueRef asI32Ptr = LLVMBuildPointerCast(builder, arrDataPtr, LLVMPointerType(int32_t_, 0), "al_i32p");
            LLVMValueRef minusOne = LLVMConstInt(int32_t_, (uint64_t)-1, 1);
            LLVMValueRef al_idx[] = { minusOne };
            LLVMValueRef lenPtr = LLVMBuildGEP2(builder, int32_t_, asI32Ptr, al_idx, 1, "al_hdr");
            return LLVMBuildLoad2(builder, int32_t_, lenPtr, "al_len");
        });

        registerBuiltin("array_slice", i8p, {i8p, int32_t_, int32_t_, int32_t_}, false,
        [this,i8p](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            LLVMValueRef arr = args[0];
            LLVMValueRef start = args[1];
            LLVMValueRef end = args[2];
            LLVMValueRef elemSize = args[3];
            LLVMValueRef len = generateBuiltinCall("array_length", {arr});
            LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef cmpStart = LLVMBuildICmp(builder, LLVMIntSLT, start, zero, "sl_cs");
            LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
            LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
            LLVMBasicBlockRef fixS = LLVMAppendBasicBlockInContext(ctx_, fn, "sl_fixs");
            LLVMBasicBlockRef contS = LLVMAppendBasicBlockInContext(ctx_, fn, "sl_conts");
            LLVMBuildCondBr(builder, cmpStart, fixS, contS);
            LLVMPositionBuilderAtEnd(builder, fixS);
            LLVMBuildBr(builder, contS);
            LLVMPositionBuilderAtEnd(builder, contS);
            LLVMValueRef sPhi = LLVMBuildPhi(builder, int32_t_, "sl_s");
            { LLVMValueRef vals[] = { zero }; LLVMBasicBlockRef blks[] = { fixS }; LLVMAddIncoming(sPhi, vals, blks, 1); }
            { LLVMValueRef vals[] = { start }; LLVMBasicBlockRef blks[] = { cur }; LLVMAddIncoming(sPhi, vals, blks, 1); }
            LLVMValueRef cmpEnd = LLVMBuildICmp(builder, LLVMIntSGT, end, len, "sl_ce");
            LLVMBasicBlockRef fixE = LLVMAppendBasicBlockInContext(ctx_, fn, "sl_fixe");
            LLVMBasicBlockRef contE = LLVMAppendBasicBlockInContext(ctx_, fn, "sl_conte");
            LLVMBuildCondBr(builder, cmpEnd, fixE, contE);
            LLVMPositionBuilderAtEnd(builder, fixE);
            LLVMBuildBr(builder, contE);
            LLVMPositionBuilderAtEnd(builder, contE);
            LLVMValueRef ePhi = LLVMBuildPhi(builder, int32_t_, "sl_e");
            { LLVMValueRef vals[] = { len }; LLVMBasicBlockRef blks[] = { fixE }; LLVMAddIncoming(ePhi, vals, blks, 1); }
            { LLVMValueRef vals[] = { end }; LLVMBasicBlockRef blks[] = { contS }; LLVMAddIncoming(ePhi, vals, blks, 1); }
            LLVMValueRef newLen = LLVMBuildSub(builder, ePhi, sPhi, "sl_len");
            LLVMValueRef dataBytes = LLVMBuildMul(builder, newLen, elemSize, "sl_bytes");
            LLVMValueRef total = LLVMBuildAdd(builder, dataBytes, LLVMConstInt(int32_t_, 4, 0), "sl_tot");
            LLVMValueRef buff = allocateString(total);
            LLVMValueRef hdr = LLVMBuildPointerCast(builder, buff, LLVMPointerType(int32_t_, 0), "sl_hdrp");
            LLVMBuildStore(builder, newLen, hdr);
            LLVMValueRef off4 = LLVMConstInt(int32_t_, 4, 0);
            LLVMValueRef sl_idx_data[] = { off4 };
            LLVMValueRef data = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), buff, sl_idx_data, 1, "");
            // memcpy out
            LLVMValueRef soff = LLVMBuildMul(builder, sPhi, elemSize, "sl_soff");
            LLVMValueRef sl_idx_src[] = { soff };
            LLVMValueRef src = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), arr, sl_idx_src, 1, "sl_src");
            LLVMValueRef memcpyArgs[] = { data, src, dataBytes };
            LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, memcpyArgs, 3, "");
            return data;
        });

        registerBuiltin("array_push", i8p, {i8p, i8p, int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            LLVMValueRef arr = args[0];
            LLVMValueRef elemPtr = args[1];
            LLVMValueRef elemSize = args[2];
            LLVMValueRef oldLen = generateBuiltinCall("array_length", {arr});
            LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
            LLVMValueRef newLen = LLVMBuildAdd(builder, oldLen, one, "ap_len");
            LLVMValueRef dataBytes = LLVMBuildMul(builder, newLen, elemSize, "ap_bytes");
            LLVMValueRef total = LLVMBuildAdd(builder, dataBytes, LLVMConstInt(int32_t_, 4, 0), "ap_tot");
            LLVMValueRef buff = allocateString(total);
            LLVMValueRef hdr = LLVMBuildPointerCast(builder, buff, LLVMPointerType(int32_t_, 0), "ap_hdrp");
            LLVMBuildStore(builder, newLen, hdr);
            LLVMValueRef off4b = LLVMConstInt(int32_t_, 4, 0);
            LLVMValueRef ap_idx_data[] = { off4b };
            LLVMValueRef outData = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), buff, ap_idx_data, 1, "");
            // copy old payload
            LLVMValueRef oldBytes = LLVMBuildMul(builder, oldLen, elemSize, "ap_oldb");
            LLVMValueRef memcpyArgs1[] = { outData, arr, oldBytes };
            LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, memcpyArgs1, 3, "");
            // write last element bytes
            LLVMValueRef lastOff = LLVMBuildMul(builder, oldLen, elemSize, "ap_last_off");
            LLVMValueRef ap_idx_dst[] = { lastOff };
            LLVMValueRef dst = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), outData, ap_idx_dst, 1, "ap_dst");
            LLVMValueRef memcpyArgs2[] = { dst, elemPtr, elemSize };
            LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, memcpyArgs2, 3, "");
            return outData;
        });

        registerBuiltin("array_pop", i8p, {i8p, int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            LLVMValueRef arr = args[0];
            LLVMValueRef elemSize = args[1];
            LLVMValueRef len = generateBuiltinCall("array_length", {arr});
            LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
            LLVMValueRef last = LLVMBuildSub(builder, len, one, "pop_idx");
            LLVMValueRef lastOff = LLVMBuildMul(builder, last, elemSize, "pop_off");
            LLVMValueRef pop_idx_src[] = { lastOff };
            LLVMValueRef src = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), arr, pop_idx_src, 1, "pop_src");
                        LLVMValueRef out = allocateString(elemSize);
            LLVMValueRef memcpyArgs[] = { out, src, elemSize };
            LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, memcpyArgs, 3, "");
            return out;
        });

        registerBuiltin("array_free", void_t_, {i8p}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            LLVMValueRef data = args[0];
            // free(data - 4)
            LLVMValueRef minus4 = LLVMConstInt(int32_t_, -4, 1);
            LLVMValueRef af_idx[] = { minus4 };
            LLVMValueRef base = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), data, af_idx, 1, "arr_base");
            LLVMBuildCall2(builder_, LLVMGlobalGetValueType(free_fn_), free_fn_, &base, 1, "");
            return nullptr;
        });
}

void BuiltinFunctions::registerMathFunctions() {
        registerBuiltin("abs_i32", int32_t_, {int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 1) return LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef x = args[0];
            LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef isNeg = LLVMBuildICmp(builder_, LLVMIntSLT, x, zero, "abs_isneg");
            LLVMValueRef neg = LLVMBuildSub(builder_, zero, x, "abs_neg");
            return LLVMBuildSelect(builder_, isNeg, neg, x, "abs_i32");
        });

        registerBuiltin("abs_f64", double_t_, {double_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 1) return LLVMConstReal(double_t_, 0.0);
            LLVMValueRef x = args[0];
            LLVMTypeRef xTy = LLVMTypeOf(x);
            if (LLVMGetTypeKind(xTy) == LLVMIntegerTypeKind) {
                if (xTy != int32_t_) x = LLVMBuildZExt(builder_, x, int32_t_, "to_i32");
                x = LLVMBuildSIToFP(builder_, x, double_t_, "to_double");
            } else if (LLVMGetTypeKind(xTy) == LLVMFloatTypeKind) {
                x = LLVMBuildFPExt(builder_, x, double_t_, "to_double");
            }
            LLVMValueRef zero = LLVMConstReal(double_t_, 0.0);
            LLVMValueRef isNeg = LLVMBuildFCmp(builder_, LLVMRealOLT, x, zero, "fabs_isneg");
            LLVMValueRef neg = LLVMBuildFSub(builder_, zero, x, "fabs_neg");
            return LLVMBuildSelect(builder_, isNeg, neg, x, "abs_f64");
        });

    // Integer min/max
    registerBuiltin("min_i32", int32_t_, {int32_t_, int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 2) return LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef a = args[0], b = args[1];
            LLVMValueRef lt = LLVMBuildICmp(builder_, LLVMIntSLT, a, b, "min_lt");
            return LLVMBuildSelect(builder_, lt, a, b, "min_i32");
        });
    registerBuiltin("max_i32", int32_t_, {int32_t_, int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 2) return LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef a = args[0], b = args[1];
            LLVMValueRef gt = LLVMBuildICmp(builder_, LLVMIntSGT, a, b, "max_gt");
            return LLVMBuildSelect(builder_, gt, a, b, "max_i32");
        });

        registerBuiltin("min_f64", double_t_, {double_t_, double_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 2) return LLVMConstReal(double_t_, 0.0);
            LLVMValueRef a = args[0], b = args[1];
            LLVMTypeRef aTy = LLVMTypeOf(a), bTy = LLVMTypeOf(b);
            if (LLVMGetTypeKind(aTy) == LLVMIntegerTypeKind) {
                if (aTy != int32_t_) a = LLVMBuildZExt(builder_, a, int32_t_, "a_i32");
                a = LLVMBuildSIToFP(builder_, a, double_t_, "a_f64");
            } else if (LLVMGetTypeKind(aTy) == LLVMFloatTypeKind) {
                a = LLVMBuildFPExt(builder_, a, double_t_, "a_f64");
            }
            if (LLVMGetTypeKind(bTy) == LLVMIntegerTypeKind) {
                if (bTy != int32_t_) b = LLVMBuildZExt(builder_, b, int32_t_, "b_i32");
                b = LLVMBuildSIToFP(builder_, b, double_t_, "b_f64");
            } else if (LLVMGetTypeKind(bTy) == LLVMFloatTypeKind) {
                b = LLVMBuildFPExt(builder_, b, double_t_, "b_f64");
            }
            LLVMValueRef lt = LLVMBuildFCmp(builder_, LLVMRealOLT, a, b, "min_olt");
            return LLVMBuildSelect(builder_, lt, a, b, "min_f64");
        });
    registerBuiltin("max_f64", double_t_, {double_t_, double_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 2) return LLVMConstReal(double_t_, 0.0);
            LLVMValueRef a = args[0], b = args[1];
            LLVMTypeRef aTy = LLVMTypeOf(a), bTy = LLVMTypeOf(b);
            if (LLVMGetTypeKind(aTy) == LLVMIntegerTypeKind) {
                if (aTy != int32_t_) a = LLVMBuildZExt(builder_, a, int32_t_, "a_i32");
                a = LLVMBuildSIToFP(builder_, a, double_t_, "a_f64");
            } else if (LLVMGetTypeKind(aTy) == LLVMFloatTypeKind) {
                a = LLVMBuildFPExt(builder_, a, double_t_, "a_f64");
            }
            if (LLVMGetTypeKind(bTy) == LLVMIntegerTypeKind) {
                if (bTy != int32_t_) b = LLVMBuildZExt(builder_, b, int32_t_, "b_i32");
                b = LLVMBuildSIToFP(builder_, b, double_t_, "b_f64");
            } else if (LLVMGetTypeKind(bTy) == LLVMFloatTypeKind) {
                b = LLVMBuildFPExt(builder_, b, double_t_, "b_f64");
            }
            LLVMValueRef gt = LLVMBuildFCmp(builder_, LLVMRealOGT, a, b, "max_ogt");
            return LLVMBuildSelect(builder_, gt, a, b, "max_f64");
        });

        registerBuiltin("min", double_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 2) return LLVMConstReal(double_t_, 0.0);
            auto allInt = [&](const std::vector<LLVMValueRef>& av){
                for (auto v : av) if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind) return false; return true; };
            if (allInt(args)) {
                LLVMValueRef a = args[0], b = args[1];
                auto toI32 = [&](LLVMValueRef v){ return (LLVMTypeOf(v) == int32_t_) ? v : LLVMBuildZExt(builder_, v, int32_t_, "i32"); };
                a = toI32(a); b = toI32(b);
                std::vector<LLVMValueRef> iargs = { a, b };
                return generateBuiltinCall("min_i32", iargs);
            }
                        auto toF64 = [&](LLVMValueRef v){ LLVMTypeRef ty = LLVMTypeOf(v); if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) { if (ty != int32_t_) v = LLVMBuildZExt(builder_, v, int32_t_, "i32"); return LLVMBuildSIToFP(builder_, v, double_t_, "f64"); } if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) return LLVMBuildFPExt(builder_, v, double_t_, "f64"); return v; };
            std::vector<LLVMValueRef> dargs = { toF64(args[0]), toF64(args[1]) };
            return generateBuiltinCall("min_f64", dargs);
        });

        registerBuiltin("max", double_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 2) return LLVMConstReal(double_t_, 0.0);
            auto allInt = [&](const std::vector<LLVMValueRef>& av){
                for (auto v : av) if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind) return false; return true; };
            if (allInt(args)) {
                LLVMValueRef a = args[0], b = args[1];
                auto toI32 = [&](LLVMValueRef v){ return (LLVMTypeOf(v) == int32_t_) ? v : LLVMBuildZExt(builder_, v, int32_t_, "i32"); };
                a = toI32(a); b = toI32(b);
                std::vector<LLVMValueRef> iargs = { a, b };
                return generateBuiltinCall("max_i32", iargs);
            }
            auto toF64 = [&](LLVMValueRef v){ LLVMTypeRef ty = LLVMTypeOf(v); if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) { if (ty != int32_t_) v = LLVMBuildZExt(builder_, v, int32_t_, "i32"); return LLVMBuildSIToFP(builder_, v, double_t_, "f64"); } if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) return LLVMBuildFPExt(builder_, v, double_t_, "f64"); return v; };
            std::vector<LLVMValueRef> dargs = { toF64(args[0]), toF64(args[1]) };
            return generateBuiltinCall("max_f64", dargs);
        });

    // Clamp helpers
    registerBuiltin("clamp_i32", int32_t_, {int32_t_, int32_t_, int32_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 3) return LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef x = args[0], lo = args[1], hi = args[2];
            // t = max(x, lo)
            LLVMValueRef ge_lo = LLVMBuildICmp(builder_, LLVMIntSGE, x, lo, "ge_lo");
            LLVMValueRef t = LLVMBuildSelect(builder_, ge_lo, x, lo, "t");
            // r = min(t, hi)
            LLVMValueRef le_hi = LLVMBuildICmp(builder_, LLVMIntSLE, t, hi, "le_hi");
            return LLVMBuildSelect(builder_, le_hi, t, hi, "clamp_i32");
        });
    registerBuiltin("clamp_f64", double_t_, {double_t_, double_t_, double_t_}, false,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 3) return LLVMConstReal(double_t_, 0.0);
            LLVMValueRef x = args[0], lo = args[1], hi = args[2];
            auto toF64 = [&](LLVMValueRef v, const char* name) {
                LLVMTypeRef ty = LLVMTypeOf(v);
                if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) {
                    if (ty != int32_t_) v = LLVMBuildZExt(builder_, v, int32_t_, (std::string(name) + "_i32").c_str());
                    return LLVMBuildSIToFP(builder_, v, double_t_, (std::string(name) + "_f64").c_str());
                } else if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) {
                    return LLVMBuildFPExt(builder_, v, double_t_, (std::string(name) + "_f64").c_str());
                }
                return v;
            };
            x = toF64(x, "x"); lo = toF64(lo, "lo"); hi = toF64(hi, "hi");
                        LLVMValueRef ge_lo = LLVMBuildFCmp(builder_, LLVMRealOGE, x, lo, "oge_lo");
            LLVMValueRef t = LLVMBuildSelect(builder_, ge_lo, x, lo, "t");
            // r = min(t, hi)
            LLVMValueRef le_hi = LLVMBuildFCmp(builder_, LLVMRealOLE, t, hi, "ole_hi");
            return LLVMBuildSelect(builder_, le_hi, t, hi, "clamp_f64");
        });

            registerBuiltin("clamp", /*returnType placeholder*/ double_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            if (args.size() != 3) {
                                return LLVMConstReal(double_t_, 0.0);
            }
            auto isAllInt = [&](const std::vector<LLVMValueRef>& av) {
                for (auto v : av) if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind) return false;
                return true;
            };
            if (isAllInt(args)) {
                                LLVMValueRef a0 = args[0], a1 = args[1], a2 = args[2];
                auto toI32 = [&](LLVMValueRef v) {
                    LLVMTypeRef ty = LLVMTypeOf(v);
                    if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind && ty != int32_t_) return LLVMBuildZExt(builder_, v, int32_t_, "i32");
                    if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind) return LLVMBuildPtrToInt(builder_, v, int32_t_, "i32ptr");
                    return (LLVMTypeOf(v) == int32_t_) ? v : LLVMBuildZExt(builder_, v, int32_t_, "i32any");
                };
                a0 = toI32(a0); a1 = toI32(a1); a2 = toI32(a2);
                std::vector<LLVMValueRef> iargs = { a0, a1, a2 };
                return generateBuiltinCall("clamp_i32", iargs);
            }
                        auto toF64 = [&](LLVMValueRef v) {
                LLVMTypeRef ty = LLVMTypeOf(v);
                if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) {
                    if (ty != int32_t_) v = LLVMBuildZExt(builder_, v, int32_t_, "i32");
                    return LLVMBuildSIToFP(builder_, v, double_t_, "f64");
                } else if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) {
                    return LLVMBuildFPExt(builder_, v, double_t_, "f64");
                } else if (LLVMGetTypeKind(ty) == LLVMDoubleTypeKind) {
                    return v;
                }
                                return LLVMConstReal(double_t_, 0.0);
            };
            std::vector<LLVMValueRef> dargs = { toF64(args[0]), toF64(args[1]), toF64(args[2]) };
            return generateBuiltinCall("clamp_f64", dargs);
        });
}

void BuiltinFunctions::registerFormatFunctions() {
                registerBuiltin("format", int8ptr_t_, {}, /*isVariadic*/ true,
        [this](LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
            return formatString(builder, args);
        }
    );
}

bool BuiltinFunctions::isBuiltin(const std::string& functionName) const {
    return builtins_.find(functionName) != builtins_.end();
}

const BuiltinFunction* BuiltinFunctions::getBuiltin(const std::string& functionName) const {
    auto it = builtins_.find(functionName);
    return it != builtins_.end() ? it->second.get() : nullptr;
}

LLVMValueRef BuiltinFunctions::generateBuiltinCall(const std::string& functionName, const std::vector<LLVMValueRef>& args) {
    auto it = builtins_.find(functionName);
    if (it == builtins_.end()) {
        return nullptr; // Not a built-in function
    }
    
    return it->second->implementation(builder_, args);
}

void BuiltinFunctions::registerWithGlobalMap(std::unordered_map<std::string, LLVMValueRef>* functionMap,
                                           std::unordered_map<std::string, std::vector<LLVMTypeRef>>* functionParamTypes,
                                           std::unordered_map<std::string, bool>* variadicFunctions) {
    for (const auto& pair : builtins_) {
        const std::string& name = pair.first;
        const BuiltinFunction* builtin = pair.second.get();
        
                LLVMTypeRef funcType = LLVMFunctionType(
            builtin->returnType,
            builtin->paramTypes.empty() ? nullptr : const_cast<LLVMTypeRef*>(builtin->paramTypes.data()),
            static_cast<unsigned>(builtin->paramTypes.size()),
            builtin->isVariadic ? 1 : 0
        );
        
                LLVMValueRef func = LLVMGetNamedFunction(module_, name.c_str());
        if (!func) {
            func = LLVMAddFunction(module_, name.c_str(), funcType);
                                                LLVMSetLinkage(func, LLVMExternalLinkage);
        } else {
                        LLVMSetLinkage(func, LLVMExternalLinkage);
        }

        (*functionMap)[name] = func;
        (*functionParamTypes)[name] = builtin->paramTypes;
        if (variadicFunctions) {
            (*variadicFunctions)[name] = builtin->isVariadic;
        }
    }
}

void BuiltinFunctions::registerTypesWithExpressionCodeGen(ExpressionCodeGen* exprGen) {
    if (!exprGen) return;
    for (const auto &pair : builtins_) {
        const std::string &name = pair.first;
        const BuiltinFunction* bi = pair.second.get();
        if (!bi) continue;
        
        // Special handling for str_split which returns str[]
        if (name == "str_split") {
            exprGen->declareFunctionType(name, QuarkType::Array, SourceLocation(), QuarkType::String, 0);
            continue;
        }
        
        QuarkType qt = QuarkType::Unknown;
        if (bi->returnType == int32_t_) qt = QuarkType::Int;
        else if (bi->returnType == float_t_) qt = QuarkType::Float;
        else if (bi->returnType == double_t_) qt = QuarkType::Double;
        else if (bi->returnType == int8ptr_t_) qt = QuarkType::String;
        else if (bi->returnType == bool_t_) qt = QuarkType::Boolean;
        else if (bi->returnType == LLVMVoidTypeInContext(ctx_)) qt = QuarkType::Void;

        if (qt != QuarkType::Unknown) {
            exprGen->declareFunctionType(name, qt, SourceLocation());
        }
    }
}

void BuiltinFunctions::registerBuiltin(const std::string& name, LLVMTypeRef returnType,
                                     std::vector<LLVMTypeRef> paramTypes, bool isVariadic,
                                     std::function<LLVMValueRef(LLVMBuilderRef, const std::vector<LLVMValueRef>&)> implementation) {
    builtins_[name] = std::make_unique<BuiltinFunction>(name, returnType, std::move(paramTypes), isVariadic, std::move(implementation));
}

void BuiltinFunctions::declareCLibraryFunctions() {
        {
        LLVMTypeRef paramTypes[] = {int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 1, 0);
        malloc_fn_ = LLVMAddFunction(module_, "malloc", funcType);
    }
    
    // free: void free(void* ptr)
    {
        LLVMTypeRef paramTypes[] = {int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(void_t_, paramTypes, 1, 0);
        free_fn_ = LLVMAddFunction(module_, "free", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int32_t_, paramTypes, 1, 0);
        strlen_fn_ = LLVMAddFunction(module_, "strlen", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 2, 0);
        strcpy_fn_ = LLVMAddFunction(module_, "strcpy", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 2, 0);
        strcat_fn_ = LLVMAddFunction(module_, "strcat", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int8ptr_t_, int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 3, 0);
        strncpy_fn_ = LLVMAddFunction(module_, "strncpy", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 2, 0);
        strstr_fn_ = LLVMAddFunction(module_, "strstr", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int8ptr_t_, int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 3, 0);
        memcpy_fn_ = LLVMAddFunction(module_, "memcpy", funcType);
    }
    
        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int32_t_, int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 3, 0);
        memset_fn_ = LLVMAddFunction(module_, "memset", funcType);
    }

            {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int32_t_, int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int32_t_, paramTypes, 3, 1);
        snprintf_fn_ = LLVMAddFunction(module_, "snprintf", funcType);
    }

        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int32_t_, paramTypes, 1, 1);
        printf_fn_ = LLVMAddFunction(module_, "printf", funcType);
    }

        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int32_t_, paramTypes, 1, 0);
        fflush_fn_ = LLVMAddFunction(module_, "fflush", funcType);
    }

            {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 2, 0);
        gets_s_fn_ = LLVMAddFunction(module_, "gets_s", funcType);
    }

        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int8ptr_t_, paramTypes, 2, 0);
        strchr_fn_ = LLVMAddFunction(module_, "strchr", funcType);
    }

        {
        LLVMTypeRef paramTypes[] = {int8ptr_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int32_t_, paramTypes, 1, 0);
        atoi_fn_ = LLVMAddFunction(module_, "atoi", funcType);
    }

    // strncmp: int strncmp(const char* s1, const char* s2, size_t n)
    {
        LLVMTypeRef paramTypes[] = {int8ptr_t_, int8ptr_t_, int32_t_};
        LLVMTypeRef funcType = LLVMFunctionType(int32_t_, paramTypes, 3, 0);
        strncmp_fn_ = LLVMAddFunction(module_, "strncmp", funcType);
    }
}

LLVMValueRef BuiltinFunctions::createStringLiteral(const std::string& str) {
    return LLVMBuildGlobalStringPtr(builder_, str.c_str(), "str_literal");
}

LLVMValueRef BuiltinFunctions::allocateString(LLVMValueRef size) {
    return LLVMBuildCall2(builder_, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &size, 1, "malloc_str");
}

LLVMValueRef BuiltinFunctions::intToString(LLVMValueRef intVal) {
        LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
    LLVMValueRef fmt = createStringLiteral("%d");
    LLVMValueRef sizeArgs[] = { nullPtr, zero, fmt, intVal };
    LLVMTypeRef snprintfTy = LLVMGlobalGetValueType(snprintf_fn_);
    LLVMValueRef needed = LLVMBuildCall2(builder_, snprintfTy, snprintf_fn_, sizeArgs, 4, "int_str_size");

    // Allocate size + 1
    LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
    LLVMValueRef allocSize = LLVMBuildAdd(builder_, needed, one, "int_str_alloc");
    LLVMValueRef buff = allocateString(allocSize);

    // Write into buffer
    LLVMValueRef writeArgs[] = { buff, allocSize, fmt, intVal };
    LLVMBuildCall2(builder_, snprintfTy, snprintf_fn_, writeArgs, 4, "");
    return buff;
}

LLVMValueRef BuiltinFunctions::boolToString(LLVMValueRef boolVal) {
        LLVMValueRef trueStr = createStringLiteral("true");
    LLVMValueRef falseStr = createStringLiteral("false");
    // Compute lengths
    LLVMValueRef lenTrue = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &trueStr, 1, "len_true");
    LLVMValueRef lenFalse = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &falseStr, 1, "len_false");
    // Select len and src
    LLVMValueRef selLen = LLVMBuildSelect(builder_, boolVal, lenTrue, lenFalse, "sel_len");
    LLVMValueRef selSrc = LLVMBuildSelect(builder_, boolVal, trueStr, falseStr, "sel_src");
    // Allocate len+1
    LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
    LLVMValueRef allocSize = LLVMBuildAdd(builder_, selLen, one, "alloc_bool");
    LLVMValueRef dst = allocateString(allocSize);
    // Copy
    LLVMValueRef cpyArgs[] = { dst, selSrc };
    LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strcpy_fn_), strcpy_fn_, cpyArgs, 2, "");
    return dst;
}

LLVMValueRef BuiltinFunctions::floatToString(LLVMValueRef floatVal) {
        LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
    LLVMValueRef fmt = createStringLiteral("%.6f");
    
        LLVMValueRef doubleVal = LLVMBuildFPExt(builder_, floatVal, double_t_, "float_to_double");
    
    LLVMValueRef sizeArgs[] = { nullPtr, zero, fmt, doubleVal };
    LLVMTypeRef snprintfTy = LLVMGlobalGetValueType(snprintf_fn_);
    LLVMValueRef needed = LLVMBuildCall2(builder_, snprintfTy, snprintf_fn_, sizeArgs, 4, "float_str_size");

    // Allocate size + 1
    LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
    LLVMValueRef allocSize = LLVMBuildAdd(builder_, needed, one, "float_str_alloc");
    LLVMValueRef buff = allocateString(allocSize);

    // Write into buffer
    LLVMValueRef writeArgs[] = { buff, allocSize, fmt, doubleVal };
    LLVMBuildCall2(builder_, snprintfTy, snprintf_fn_, writeArgs, 4, "");
    return buff;
}

LLVMValueRef BuiltinFunctions::doubleToString(LLVMValueRef doubleVal) {
        LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
    LLVMValueRef fmt = createStringLiteral("%.6f");
    LLVMValueRef sizeArgs[] = { nullPtr, zero, fmt, doubleVal };
    LLVMTypeRef snprintfTy = LLVMGlobalGetValueType(snprintf_fn_);
    LLVMValueRef needed = LLVMBuildCall2(builder_, snprintfTy, snprintf_fn_, sizeArgs, 4, "double_str_size");

    // Allocate size + 1
    LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
    LLVMValueRef allocSize = LLVMBuildAdd(builder_, needed, one, "double_str_alloc");
    LLVMValueRef buff = allocateString(allocSize);

    // Write into buffer
    LLVMValueRef writeArgs[] = { buff, allocSize, fmt, doubleVal };
    LLVMBuildCall2(builder_, snprintfTy, snprintf_fn_, writeArgs, 4, "");
    return buff;
}

// Core format implementation
LLVMValueRef BuiltinFunctions::formatString(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
        if (args.empty()) return nullptr;

    LLVMValueRef formatArg = args[0];
                    LLVMValueRef brace = createStringLiteral("{}");
    LLVMValueRef percentS = createStringLiteral("%s");

        std::vector<LLVMValueRef> repArgs = { formatArg, brace, percentS };
    LLVMValueRef cFormat = stringReplace(builder, repArgs);

        LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);

        std::vector<LLVMValueRef> sizeCallArgs;
    sizeCallArgs.push_back(nullPtr);
    sizeCallArgs.push_back(zero);
    sizeCallArgs.push_back(cFormat);

        std::vector<int> allocatedFlags;
    allocatedFlags.reserve(args.size() > 0 ? args.size() - 1 : 0);
    for (size_t i = 1; i < args.size(); ++i) {
        LLVMValueRef v = args[i];
        LLVMTypeRef ty = LLVMTypeOf(v);
        LLVMValueRef asStr = nullptr;
        int allocated = 0;
        if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind) {
                        asStr = LLVMBuildPointerCast(builder, v, int8ptr_t_, "to_str");
        } else if (LLVMGetTypeKind(ty) == LLVMIntegerTypeKind) {
            unsigned w = LLVMGetIntTypeWidth(ty);
            if (w == 1) {
                asStr = boolToString(v);
                allocated = 1;
            } else {
                                LLVMValueRef i32v = (w == 32) ? v : LLVMBuildSExt(builder, v, int32_t_, "to_i32");
                asStr = intToString(i32v);
                allocated = 1;
            }
        } else if (LLVMGetTypeKind(ty) == LLVMFloatTypeKind) {
            asStr = floatToString(v);
            allocated = 1;
        } else if (LLVMGetTypeKind(ty) == LLVMDoubleTypeKind) {
            asStr = doubleToString(v);
            allocated = 1;
        } else {
            // Fallback: cast to i8*
            asStr = LLVMBuildPointerCast(builder, v, int8ptr_t_, "to_str_any");
        }
        sizeCallArgs.push_back(asStr);
        allocatedFlags.push_back(allocated);
    }

    LLVMTypeRef snprintfTy = LLVMGlobalGetValueType(snprintf_fn_);
    LLVMValueRef needed = LLVMBuildCall2(builder, snprintfTy, snprintf_fn_, sizeCallArgs.data(), (unsigned)sizeCallArgs.size(), "fmt_size");

    // Allocate buffer (needed + 1)
    LLVMValueRef one = LLVMConstInt(int32_t_, 1, 0);
    LLVMValueRef allocSize = LLVMBuildAdd(builder, needed, one, "fmt_alloc");
    LLVMValueRef buffer = allocateString(allocSize);

        std::vector<LLVMValueRef> writeArgs;
    writeArgs.push_back(buffer);
    writeArgs.push_back(allocSize);
    writeArgs.push_back(cFormat);
    for (size_t i = 3; i < sizeCallArgs.size(); ++i) {
        writeArgs.push_back(sizeCallArgs[i]);
    }
    LLVMBuildCall2(builder, snprintfTy, snprintf_fn_, writeArgs.data(), (unsigned)writeArgs.size(), "");

    for (size_t i = 0; i < allocatedFlags.size(); ++i) {
        if (allocatedFlags[i]) {
            LLVMValueRef toFree = writeArgs[3 + i];
            LLVMBuildCall2(builder_, LLVMGlobalGetValueType(free_fn_), free_fn_, &toFree, 1, "");
        }
    }
    LLVMBuildCall2(builder_, LLVMGlobalGetValueType(free_fn_), free_fn_, &cFormat, 1, "");
    return buffer;
}

LLVMValueRef BuiltinFunctions::stringSlice(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 3) return nullptr;
    
    LLVMValueRef str = args[0];
    LLVMValueRef start = args[1];
    LLVMValueRef end = args[2];
    
        LLVMValueRef length = LLVMBuildSub(builder, end, start, "slice_len");
    
    // Add 1 for null terminator
    LLVMValueRef allocSize = LLVMBuildAdd(builder, length, LLVMConstInt(int32_t_, 1, 0), "alloc_size");
    
    // Allocate memory for result
    LLVMValueRef result = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &allocSize, 1, "slice_result");
    
        LLVMValueRef ss_idx_src[] = { start };
    LLVMValueRef srcPtr = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), str, ss_idx_src, 1, "src_ptr");
    
    // Copy substring using memcpy
    LLVMValueRef copyArgs[] = {result, srcPtr, length};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, copyArgs, 3, "");
    
    // Null terminate the result
    LLVMValueRef ss_idx_end[] = { length };
    LLVMValueRef nullPos = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), result, ss_idx_end, 1, "null_pos");
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt8TypeInContext(ctx_), 0, 0), nullPos);
    
    return result;
}

LLVMValueRef BuiltinFunctions::stringConcat(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 2) return nullptr;
    
    LLVMValueRef str1 = args[0];
    LLVMValueRef str2 = args[1];
    
    // Get lengths of both strings
    LLVMValueRef len1 = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &str1, 1, "len1");
    LLVMValueRef len2 = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &str2, 1, "len2");
    
        LLVMValueRef totalLen = LLVMBuildAdd(builder, len1, len2, "total_len");
    LLVMValueRef allocSize = LLVMBuildAdd(builder, totalLen, LLVMConstInt(int32_t_, 1, 0), "alloc_size");
    
    // Allocate memory for result
    LLVMValueRef result = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &allocSize, 1, "concat_result");
    
    // Copy first string
    LLVMValueRef strcpyArgs[] = {result, str1};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcpy_fn_), strcpy_fn_, strcpyArgs, 2, "");
    
    // Concatenate second string  
    LLVMValueRef strcatArgs[] = {result, str2};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcat_fn_), strcat_fn_, strcatArgs, 2, "");
    
    return result;
}

LLVMValueRef BuiltinFunctions::stringFind(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 2) return nullptr;
    
    LLVMValueRef haystack = args[0];
    LLVMValueRef needle = args[1];
    
    // Use strstr to find substring
    LLVMValueRef strstrArgs[] = {haystack, needle};
    LLVMValueRef found = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strstr_fn_), strstr_fn_, strstrArgs, 2, "strstr_result");
    
    // Check if found (not NULL)
    LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef isFound = LLVMBuildICmp(builder, LLVMIntNE, found, nullPtr, "is_found");
    
    // Return boolean presence
    return isFound;
}

LLVMValueRef BuiltinFunctions::stringReplace(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 3) return nullptr;
    
    LLVMValueRef str = args[0];
    LLVMValueRef oldStr = args[1];
    LLVMValueRef newStr = args[2];
    
    // Get string lengths
    LLVMValueRef strLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &str, 1, "str_len");
    LLVMValueRef oldLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &oldStr, 1, "old_len");
    LLVMValueRef newLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &newStr, 1, "new_len");
    
        LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
    LLVMValueRef oldIsEmpty = LLVMBuildICmp(builder, LLVMIntEQ, oldLen, zero, "old_is_empty");
    
    LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder);
    LLVMValueRef function = LLVMGetBasicBlockParent(currentBB);
    LLVMBasicBlockRef emptyBB = LLVMAppendBasicBlockInContext(ctx_, function, "empty_old");
    LLVMBasicBlockRef replaceBB = LLVMAppendBasicBlockInContext(ctx_, function, "do_replace");
        LLVMBasicBlockRef afterBB = LLVMAppendBasicBlockInContext(ctx_, function, "after_replace");
        LLVMValueRef resultAlloca = LLVMBuildAlloca(builder, int8ptr_t_, "strrep_ret");
    
    LLVMBuildCondBr(builder, oldIsEmpty, emptyBB, replaceBB);
    
        LLVMPositionBuilderAtEnd(builder, emptyBB);
    LLVMValueRef copySize = LLVMBuildAdd(builder, strLen, LLVMConstInt(int32_t_, 1, 0), "copy_size");
    LLVMValueRef emptyCopy = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &copySize, 1, "empty_copy");
    LLVMValueRef emptyCopyArgs[] = {emptyCopy, str};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcpy_fn_), strcpy_fn_, emptyCopyArgs, 2, "");
        LLVMBuildStore(builder, emptyCopy, resultAlloca);
    LLVMBuildBr(builder, afterBB);
    
        LLVMPositionBuilderAtEnd(builder, replaceBB);
    
        LLVMValueRef four = LLVMConstInt(int32_t_, 4, 0);
    LLVMValueRef extra = LLVMConstInt(int32_t_, 1024, 0);
    LLVMValueRef bufferSize = LLVMBuildAdd(builder, LLVMBuildMul(builder, strLen, four, "times_four"), extra, "buffer_size");
    LLVMValueRef result = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &bufferSize, 1, "result");
    
        LLVMBuildStore(builder, LLVMConstInt(LLVMInt8TypeInContext(ctx_), 0, 0), result);
    
            LLVMValueRef searchStart = str;
    
    LLVMBasicBlockRef loopBB = LLVMAppendBasicBlockInContext(ctx_, function, "search_loop");
    LLVMBasicBlockRef foundBB = LLVMAppendBasicBlockInContext(ctx_, function, "found_match");
    LLVMBasicBlockRef doneBB = LLVMAppendBasicBlockInContext(ctx_, function, "search_done");
    
    LLVMBuildBr(builder, loopBB);
    
    // Search loop
    LLVMPositionBuilderAtEnd(builder, loopBB);
    LLVMValueRef searchPhi = LLVMBuildPhi(builder, int8ptr_t_, "search_phi");
    
    // Search for the old string
    LLVMValueRef searchArgs[] = {searchPhi, oldStr};
    LLVMValueRef foundPos = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strstr_fn_), strstr_fn_, searchArgs, 2, "found_pos");
    
    LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef found = LLVMBuildICmp(builder, LLVMIntNE, foundPos, nullPtr, "found");
    
    LLVMBuildCondBr(builder, found, foundBB, doneBB);
    
        LLVMPositionBuilderAtEnd(builder, foundBB);
    
        LLVMValueRef prefixLen64 = LLVMBuildPtrDiff2(builder, LLVMInt8TypeInContext(ctx_), foundPos, searchPhi, "prefix_len64");
    LLVMValueRef prefixLen = LLVMBuildTrunc(builder, prefixLen64, int32_t_, "prefix_len");
    
        LLVMValueRef tempSize = LLVMBuildAdd(builder, prefixLen, LLVMConstInt(int32_t_, 1, 0), "temp_size");
    LLVMValueRef tempBuf = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &tempSize, 1, "temp_buf");
    LLVMValueRef copyPrefixArgs[] = {tempBuf, searchPhi, prefixLen};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, copyPrefixArgs, 3, "");
    
    // Null terminate temp buffer
    LLVMValueRef sr_idx_end[] = { prefixLen };
    LLVMValueRef tempEnd = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), tempBuf, sr_idx_end, 1, "temp_end");
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt8TypeInContext(ctx_), 0, 0), tempEnd);
    
    // Append prefix to result
    LLVMValueRef appendPrefixArgs[] = {result, tempBuf};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcat_fn_), strcat_fn_, appendPrefixArgs, 2, "");
    
    // Free temporary buffer
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(free_fn_), free_fn_, &tempBuf, 1, "");
    
    // Append replacement string
    LLVMValueRef appendNewArgs[] = {result, newStr};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcat_fn_), strcat_fn_, appendNewArgs, 2, "");
    
        LLVMValueRef sr_idx_next[] = { oldLen };
    LLVMValueRef nextSearch = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), foundPos, sr_idx_next, 1, "next_search");
    LLVMBuildBr(builder, loopBB);
    
        LLVMPositionBuilderAtEnd(builder, doneBB);
    LLVMValueRef finalSearch = LLVMBuildPhi(builder, int8ptr_t_, "final_search");
    
        LLVMValueRef appendRemainingArgs[] = {result, finalSearch};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcat_fn_), strcat_fn_, appendRemainingArgs, 2, "");
        LLVMBuildStore(builder, result, resultAlloca);
    LLVMBuildBr(builder, afterBB);
    
    // Set up phi node values
    LLVMValueRef searchPhiValues[] = {searchStart, nextSearch};
    LLVMBasicBlockRef searchPhiBlocks[] = {replaceBB, foundBB};
    LLVMAddIncoming(searchPhi, searchPhiValues, searchPhiBlocks, 2);
    
    LLVMValueRef finalSearchValues[] = {searchPhi};
    LLVMBasicBlockRef finalSearchBlocks[] = {loopBB};
    LLVMAddIncoming(finalSearch, finalSearchValues, finalSearchBlocks, 1);
    
        LLVMPositionBuilderAtEnd(builder, afterBB);
    LLVMBasicBlockRef contBB = LLVMAppendBasicBlockInContext(ctx_, function, "after_replace_cont");
    LLVMBuildBr(builder, contBB);
    LLVMPositionBuilderAtEnd(builder, contBB);
    LLVMValueRef finalResult = LLVMBuildLoad2(builder, int8ptr_t_, resultAlloca, "final_result");
    return finalResult;
}

LLVMValueRef BuiltinFunctions::stringSplit(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 2) return nullptr;
    
    LLVMValueRef str = args[0];
    LLVMValueRef delimiter = args[1];
    
    // Get delimiter length
    LLVMValueRef delimLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &delimiter, 1, "delim_len");
    
    // Get the current function and create basic blocks
    LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder);
    LLVMValueRef function = LLVMGetBasicBlockParent(currentBB);
    
    // First pass: count occurrences of delimiter to determine array size
    LLVMBasicBlockRef countLoopBB = LLVMAppendBasicBlockInContext(ctx_, function, "split_count_loop");
    LLVMBasicBlockRef countFoundBB = LLVMAppendBasicBlockInContext(ctx_, function, "split_count_found");
    LLVMBasicBlockRef countDoneBB = LLVMAppendBasicBlockInContext(ctx_, function, "split_count_done");
    
    // Initialize count to 1 (minimum number of parts)
    LLVMValueRef countAlloca = LLVMBuildAlloca(builder, int32_t_, "split_count");
    LLVMBuildStore(builder, LLVMConstInt(int32_t_, 1, 0), countAlloca);
    LLVMValueRef searchPtrAlloca = LLVMBuildAlloca(builder, int8ptr_t_, "search_ptr");
    LLVMBuildStore(builder, str, searchPtrAlloca);
    
    LLVMBuildBr(builder, countLoopBB);
    
    // Count loop
    LLVMPositionBuilderAtEnd(builder, countLoopBB);
    LLVMValueRef searchPtr = LLVMBuildLoad2(builder, int8ptr_t_, searchPtrAlloca, "cur_search");
    LLVMValueRef searchArgs[] = {searchPtr, delimiter};
    LLVMValueRef foundPos = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strstr_fn_), strstr_fn_, searchArgs, 2, "found_delim");
    LLVMValueRef nullPtr = LLVMConstNull(int8ptr_t_);
    LLVMValueRef isFound = LLVMBuildICmp(builder, LLVMIntNE, foundPos, nullPtr, "is_found");
    LLVMBuildCondBr(builder, isFound, countFoundBB, countDoneBB);
    
    // Found delimiter - increment count and advance search pointer
    LLVMPositionBuilderAtEnd(builder, countFoundBB);
    LLVMValueRef oldCount = LLVMBuildLoad2(builder, int32_t_, countAlloca, "old_count");
    LLVMValueRef newCount = LLVMBuildAdd(builder, oldCount, LLVMConstInt(int32_t_, 1, 0), "new_count");
    LLVMBuildStore(builder, newCount, countAlloca);
    // Advance past delimiter
    LLVMValueRef sp_idx_next[] = { delimLen };
    LLVMValueRef nextPtr = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), foundPos, sp_idx_next, 1, "next_ptr");
    LLVMBuildStore(builder, nextPtr, searchPtrAlloca);
    LLVMBuildBr(builder, countLoopBB);
    
    // Done counting
    LLVMPositionBuilderAtEnd(builder, countDoneBB);
    LLVMValueRef totalCount = LLVMBuildLoad2(builder, int32_t_, countAlloca, "total_count");
    
    // Allocate array: (count + 1) * sizeof(char*) for null terminator, plus 4 bytes for length header
    LLVMValueRef ptrSize = LLVMConstInt(int32_t_, 8, 0); // sizeof(char*)
    LLVMValueRef countPlusOne = LLVMBuildAdd(builder, totalCount, LLVMConstInt(int32_t_, 1, 0), "count_plus_one");
    LLVMValueRef arrayBytes = LLVMBuildMul(builder, countPlusOne, ptrSize, "array_bytes");
    LLVMValueRef totalBytes = LLVMBuildAdd(builder, arrayBytes, LLVMConstInt(int32_t_, 4, 0), "total_bytes");
    LLVMValueRef rawBuffer = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &totalBytes, 1, "split_buf");
    
    // Store length in header (first 4 bytes)
    LLVMValueRef headerPtr = LLVMBuildPointerCast(builder, rawBuffer, LLVMPointerType(int32_t_, 0), "header_ptr");
    LLVMBuildStore(builder, totalCount, headerPtr);
    
    // Get pointer to data (after header)
    LLVMValueRef sp_idx_data[] = { LLVMConstInt(int32_t_, 4, 0) };
    LLVMValueRef dataPtr = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), rawBuffer, sp_idx_data, 1, "data_ptr");
    LLVMValueRef resultArray = LLVMBuildPointerCast(builder, dataPtr, LLVMPointerType(int8ptr_t_, 0), "result_array");
    
    // Second pass: actually split the string
    LLVMBasicBlockRef splitLoopBB = LLVMAppendBasicBlockInContext(ctx_, function, "split_loop");
    LLVMBasicBlockRef splitFoundBB = LLVMAppendBasicBlockInContext(ctx_, function, "split_found");
    LLVMBasicBlockRef splitDoneBB = LLVMAppendBasicBlockInContext(ctx_, function, "split_done");
    
    // Reset search pointer and initialize index
    LLVMBuildStore(builder, str, searchPtrAlloca);
    LLVMValueRef indexAlloca = LLVMBuildAlloca(builder, int32_t_, "split_index");
    LLVMBuildStore(builder, LLVMConstInt(int32_t_, 0, 0), indexAlloca);
    LLVMValueRef startPtrAlloca = LLVMBuildAlloca(builder, int8ptr_t_, "start_ptr");
    LLVMBuildStore(builder, str, startPtrAlloca);
    
    LLVMBuildBr(builder, splitLoopBB);
    
    // Split loop
    LLVMPositionBuilderAtEnd(builder, splitLoopBB);
    LLVMValueRef curSearchPtr = LLVMBuildLoad2(builder, int8ptr_t_, searchPtrAlloca, "cur_search2");
    LLVMValueRef searchArgs2[] = {curSearchPtr, delimiter};
    LLVMValueRef foundPos2 = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strstr_fn_), strstr_fn_, searchArgs2, 2, "found_delim2");
    LLVMValueRef isFound2 = LLVMBuildICmp(builder, LLVMIntNE, foundPos2, nullPtr, "is_found2");
    LLVMBuildCondBr(builder, isFound2, splitFoundBB, splitDoneBB);
    
    // Found delimiter - extract substring
    LLVMPositionBuilderAtEnd(builder, splitFoundBB);
    LLVMValueRef startPtr = LLVMBuildLoad2(builder, int8ptr_t_, startPtrAlloca, "start");
    // Calculate length of this segment
    LLVMValueRef segLen64 = LLVMBuildPtrDiff2(builder, LLVMInt8TypeInContext(ctx_), foundPos2, startPtr, "seg_len64");
    LLVMValueRef segLen = LLVMBuildTrunc(builder, segLen64, int32_t_, "seg_len");
    // Allocate and copy segment
    LLVMValueRef segSize = LLVMBuildAdd(builder, segLen, LLVMConstInt(int32_t_, 1, 0), "seg_size");
    LLVMValueRef segment = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &segSize, 1, "segment");
    LLVMValueRef memcpyArgs[] = {segment, startPtr, segLen};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(memcpy_fn_), memcpy_fn_, memcpyArgs, 3, "");
    // Null terminate
    LLVMValueRef sp_idx_end[] = { segLen };
    LLVMValueRef segEnd = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), segment, sp_idx_end, 1, "seg_end");
    LLVMBuildStore(builder, LLVMConstInt(LLVMInt8TypeInContext(ctx_), 0, 0), segEnd);
    // Store in result array
    LLVMValueRef curIndex = LLVMBuildLoad2(builder, int32_t_, indexAlloca, "cur_idx");
    LLVMValueRef sp_idx_arr[] = { curIndex };
    LLVMValueRef arrSlot = LLVMBuildGEP2(builder, int8ptr_t_, resultArray, sp_idx_arr, 1, "arr_slot");
    LLVMBuildStore(builder, segment, arrSlot);
    // Increment index
    LLVMValueRef newIndex = LLVMBuildAdd(builder, curIndex, LLVMConstInt(int32_t_, 1, 0), "new_idx");
    LLVMBuildStore(builder, newIndex, indexAlloca);
    // Advance pointers past delimiter
    LLVMValueRef sp_idx_next2[] = { delimLen };
    LLVMValueRef nextStart = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), foundPos2, sp_idx_next2, 1, "next_start");
    LLVMBuildStore(builder, nextStart, searchPtrAlloca);
    LLVMBuildStore(builder, nextStart, startPtrAlloca);
    LLVMBuildBr(builder, splitLoopBB);
    
    // Done splitting - add final segment
    LLVMPositionBuilderAtEnd(builder, splitDoneBB);
    LLVMValueRef finalStart = LLVMBuildLoad2(builder, int8ptr_t_, startPtrAlloca, "final_start");
    LLVMValueRef finalLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &finalStart, 1, "final_len");
    LLVMValueRef finalSize = LLVMBuildAdd(builder, finalLen, LLVMConstInt(int32_t_, 1, 0), "final_size");
    LLVMValueRef finalSeg = LLVMBuildCall2(builder, LLVMGlobalGetValueType(malloc_fn_), malloc_fn_, &finalSize, 1, "final_seg");
    LLVMValueRef strcpyArgs[] = {finalSeg, finalStart};
    LLVMBuildCall2(builder, LLVMGlobalGetValueType(strcpy_fn_), strcpy_fn_, strcpyArgs, 2, "");
    // Store final segment
    LLVMValueRef finalIndex = LLVMBuildLoad2(builder, int32_t_, indexAlloca, "final_idx");
    LLVMValueRef sp_idx_final[] = { finalIndex };
    LLVMValueRef finalSlot = LLVMBuildGEP2(builder, int8ptr_t_, resultArray, sp_idx_final, 1, "final_slot");
    LLVMBuildStore(builder, finalSeg, finalSlot);
    // Null terminate the array
    LLVMValueRef lastIndex = LLVMBuildAdd(builder, finalIndex, LLVMConstInt(int32_t_, 1, 0), "last_idx");
    LLVMValueRef sp_idx_null[] = { lastIndex };
    LLVMValueRef nullSlot = LLVMBuildGEP2(builder, int8ptr_t_, resultArray, sp_idx_null, 1, "null_slot");
    LLVMBuildStore(builder, nullPtr, nullSlot);
    
    return resultArray;
}

LLVMValueRef BuiltinFunctions::stringStartsWith(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 2) return LLVMConstInt(bool_t_, 0, 0);
    
    LLVMValueRef str = args[0];
    LLVMValueRef prefix = args[1];
    
    // Get prefix length
    LLVMValueRef prefixLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &prefix, 1, "prefix_len");
    
    // Compare first prefixLen characters
    LLVMValueRef strncmpArgs[] = {str, prefix, prefixLen};
    LLVMValueRef cmpResult = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strncmp_fn_), strncmp_fn_, strncmpArgs, 3, "strncmp_result");
    
    // Return true if strncmp returns 0
    LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
    return LLVMBuildICmp(builder, LLVMIntEQ, cmpResult, zero, "starts_with_result");
}

LLVMValueRef BuiltinFunctions::stringEndsWith(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 2) return LLVMConstInt(bool_t_, 0, 0);
    
    LLVMValueRef str = args[0];
    LLVMValueRef suffix = args[1];
    
    // Get string and suffix lengths
    LLVMValueRef strLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &str, 1, "str_len");
    LLVMValueRef suffixLen = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &suffix, 1, "suffix_len");
    
    // Check if suffix is longer than string
    LLVMValueRef tooLong = LLVMBuildICmp(builder, LLVMIntUGT, suffixLen, strLen, "suffix_too_long");
    
    LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder);
    LLVMValueRef function = LLVMGetBasicBlockParent(currentBB);
    LLVMBasicBlockRef compareBB = LLVMAppendBasicBlockInContext(ctx_, function, "ends_with_compare");
    LLVMBasicBlockRef falseBB = LLVMAppendBasicBlockInContext(ctx_, function, "ends_with_false");
    LLVMBasicBlockRef mergeBB = LLVMAppendBasicBlockInContext(ctx_, function, "ends_with_merge");
    
    LLVMBuildCondBr(builder, tooLong, falseBB, compareBB);
    
    // Compare suffix
    LLVMPositionBuilderAtEnd(builder, compareBB);
    // Get pointer to end of string minus suffix length
    LLVMValueRef offset = LLVMBuildSub(builder, strLen, suffixLen, "end_offset");
    LLVMValueRef ew_idx[] = { offset };
    LLVMValueRef strEnd = LLVMBuildGEP2(builder, LLVMInt8TypeInContext(ctx_), str, ew_idx, 1, "str_end");
    
    // Compare
    LLVMValueRef strncmpArgs[] = {strEnd, suffix, suffixLen};
    LLVMValueRef cmpResult = LLVMBuildCall2(builder, LLVMGlobalGetValueType(strncmp_fn_), strncmp_fn_, strncmpArgs, 3, "strncmp_result");
    LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
    LLVMValueRef isEqual = LLVMBuildICmp(builder, LLVMIntEQ, cmpResult, zero, "is_equal");
    LLVMBuildBr(builder, mergeBB);
    
    // False branch
    LLVMPositionBuilderAtEnd(builder, falseBB);
    LLVMBuildBr(builder, mergeBB);
    
    // Merge
    LLVMPositionBuilderAtEnd(builder, mergeBB);
    LLVMValueRef phi = LLVMBuildPhi(builder, bool_t_, "ends_with_result");
    LLVMValueRef phiValues[] = {isEqual, LLVMConstInt(bool_t_, 0, 0)};
    LLVMBasicBlockRef phiBlocks[] = {compareBB, falseBB};
    LLVMAddIncoming(phi, phiValues, phiBlocks, 2);
    
    return phi;
}

LLVMValueRef BuiltinFunctions::stringLength(LLVMBuilderRef builder, const std::vector<LLVMValueRef>& args) {
    if (args.size() != 1) return nullptr;
    
    LLVMValueRef str = args[0];
    return LLVMBuildCall2(builder, LLVMGlobalGetValueType(strlen_fn_), strlen_fn_, &str, 1, "strlen_result");
}
