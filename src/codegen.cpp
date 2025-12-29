#include "../include/codegen.h"
#include "../include/compilation_progress.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Transforms/IPO/DeadArgumentElimination.h>
#include <llvm/Transforms/IPO/StripDeadPrototypes.h>
#include <llvm/Transforms/IPO/Internalize.h>
#include <llvm/Transforms/IPO/GlobalOpt.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/ADCE.h>
#include <stdexcept>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <chrono>
#include <cstdlib>
#include <utility>
#include "../include/error_reporter.h"

#ifdef _WIN32
#include <io.h>
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/BitReader.h>

// New Pass Manager includes
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm-c/TargetMachine.h>

#include <lld/Common/Driver.h>
#include <lld/Common/CommonLinkerContext.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/ArrayRef.h>

#ifdef _WIN32
LLD_HAS_DRIVER(coff);
#elif defined(__APPLE__)
LLD_HAS_DRIVER(macho);
#else
LLD_HAS_DRIVER(elf);
#endif

CodeGen::CodeGen(bool verbose, bool optimize, int optimizationLevel, bool freestanding,
                 std::vector<std::string> additionalLibraries,
                 std::vector<std::string> additionalLibraryPaths,
                 bool showProgress,
                 CompilationContext* compilationCtx)
    : verbose_(verbose),
      optimize_(optimize),
      freestanding_(freestanding),
      showProgress_(showProgress),
      optimizationLevel_(optimizationLevel),
      additionalLibraries_(std::move(additionalLibraries)),
      additionalLibraryPaths_(std::move(additionalLibraryPaths)),
      ctx_(compilationCtx)
{
    llvmCtx_ = LLVMContextCreate();
    module_ = LLVMModuleCreateWithNameInContext("parser_module", llvmCtx_);
    builder_ = LLVMCreateBuilderInContext(llvmCtx_);
    int32_t_ = LLVMInt32TypeInContext(llvmCtx_);
    int8ptr_t_ = LLVMPointerType(LLVMInt8TypeInContext(llvmCtx_), 0);
    float_t_ = LLVMFloatTypeInContext(llvmCtx_);
    double_t_ = LLVMDoubleTypeInContext(llvmCtx_);

#ifdef _WIN32
    fileptr_t_ = LLVMPointerType(LLVMStructCreateNamed(llvmCtx_, "struct._iobuf"), 0);
#else
    fileptr_t_ = LLVMPointerType(LLVMStructCreateNamed(llvmCtx_, "struct._IO_FILE"), 0);
#endif
    bool_t_ = LLVMInt1TypeInContext(llvmCtx_);

    initializeCodeGenModules();
    
    if (showProgress_) {
        progress_ = std::make_unique<CompilationProgress>(!verbose_);
    }
}

CodeGen::~CodeGen()
{
    if (builder_)
        LLVMDisposeBuilder(builder_);
    if (module_)
        LLVMDisposeModule(module_);
    if (llvmCtx_)
        LLVMContextDispose(llvmCtx_);
}

void CodeGen::initializeCodeGenModules()
{
    externalFunctions_ = std::make_unique<ExternalFunctions>(llvmCtx_, module_);
    builtinFunctions_ = std::make_unique<BuiltinFunctions>(llvmCtx_, module_, builder_);

    builtinFunctions_->registerAllBuiltins();

    expressionCodeGen_ = std::make_unique<ExpressionCodeGen>(llvmCtx_, module_, builder_,
                                                             externalFunctions_.get(), verbose_, ctx_);
    expressionCodeGen_->setBuiltinFunctions(builtinFunctions_.get());

    statementCodeGen_ = std::make_unique<StatementCodeGen>(llvmCtx_, module_, builder_,
                                                           externalFunctions_.get(),
                                                           expressionCodeGen_.get(), verbose_);

    expressionCodeGen_->setGlobalSymbolTables(&g_function_map_, &g_named_values_, &g_const_values_,
                                              &g_function_param_types_, &g_named_types_, &g_struct_defs_, &g_struct_types_, &g_variadic_functions_);
    statementCodeGen_->setGlobalSymbolTables(&g_function_map_, &g_named_values_, &g_const_values_,
                                             &g_function_param_types_, &g_named_types_, &g_struct_defs_, &g_struct_types_, &g_variadic_functions_);
}

void CodeGen::declareFunctions(const std::vector<FunctionAST *> &allFunctions)
{
    if (verbose_)
        printf("[codegen] starting to declare user functions\n");

    // Declare user functions
    for (auto *f : allFunctions)
    {
        if (verbose_)
            printf("[codegen] declaring function: %s\n", f->name.c_str());

        // Determine return type
        LLVMTypeRef returnType;
        if (f->returnType == "int")
            returnType = LLVMInt32TypeInContext(llvmCtx_);
        else if (f->returnType == "float")
            returnType = float_t_;
        else if (f->returnType == "double")
            returnType = double_t_;
        else if (f->returnType == "str")
            returnType = int8ptr_t_;
        else if (f->returnType == "bool")
            returnType = bool_t_;
        else if (f->returnType == "void")
            returnType = LLVMVoidTypeInContext(llvmCtx_);
        else if (f->returnType.size() > 2 && f->returnType.substr(f->returnType.size() - 2) == "[]")
        {
                        std::string elementType = f->returnType.substr(0, f->returnType.size() - 2);
            if (elementType == "str")
            {
                                returnType = LLVMPointerType(int8ptr_t_, 0);
            }
            else if (elementType == "int")
            {
                                returnType = LLVMPointerType(LLVMInt32TypeInContext(llvmCtx_), 0);
            }
            else if (elementType == "bool")
            {
                                returnType = LLVMPointerType(bool_t_, 0);
            }
            else
            {
                throw std::runtime_error("unsupported array element type in return type: " + elementType);
            }
        }
        else
        {
            // Check if it's a struct type
            auto structIt = g_struct_types_.find(f->returnType);
            if (structIt != g_struct_types_.end())
            {
                returnType = structIt->second;
            }
            else
            {
                throw std::runtime_error("unsupported return type: " + f->returnType + " (struct not found)");
            }
        }

        std::vector<LLVMTypeRef> paramTypes;
        bool isVariadic = false;

                bool isStructMethod = (f->name.find("::") != std::string::npos);
        bool isConstructor = false;
        if (isStructMethod)
        {
                        std::string structName = f->name.substr(0, f->name.find("::"));
            std::string methodName = f->name.substr(f->name.find("::") + 2);

                        isConstructor = (methodName == "new" && f->returnType == structName);

            if (!isConstructor)
            {
                auto structIt = g_struct_types_.find(structName);
                if (structIt != g_struct_types_.end())
                {
                    LLVMTypeRef selfPtrTy = LLVMPointerType(structIt->second, 0);
                    paramTypes.push_back(selfPtrTy);                     if (verbose_)
                        printf("[codegen] adding implicit 'this' (pointer) parameter of type %s* for method %s\n",
                               structName.c_str(), f->name.c_str());
                                        paramTypes.push_back(int8ptr_t_);
                    if (verbose_)
                        printf("[codegen] inserting hidden dynamic type name param for %s right after 'this'\n", f->name.c_str());
                }
            }
            else
            {
                if (verbose_)
                    printf("[codegen] skipping 'this' parameter for constructor method %s\n", f->name.c_str());
            }
        }

        for (auto &p : f->params)
        {
            const std::string &ptype = p.second;

            // Check for variadic parameter
            if (ptype == "...")
            {
                isVariadic = true;
                if (verbose_)
                    printf("[codegen] function %s is variadic\n", f->name.c_str());
                                continue;
            }

            if (ptype == "int")
                paramTypes.push_back(LLVMInt32TypeInContext(llvmCtx_));
            else if (ptype == "float")
                paramTypes.push_back(float_t_);
            else if (ptype == "double")
                paramTypes.push_back(double_t_);
            else if (ptype == "str")
                paramTypes.push_back(int8ptr_t_);
            else if (ptype == "bool")
                paramTypes.push_back(bool_t_);
            else if (ptype.size() > 2 && ptype.substr(ptype.size() - 2) == "[]")
            {
                                std::string elementType = ptype.substr(0, ptype.size() - 2);
                if (elementType == "str")
                {
                                        paramTypes.push_back(LLVMPointerType(int8ptr_t_, 0));
                }
                else if (elementType == "int")
                {
                                        paramTypes.push_back(LLVMPointerType(LLVMInt32TypeInContext(llvmCtx_), 0));
                }
                else if (elementType == "bool")
                {
                                        paramTypes.push_back(LLVMPointerType(bool_t_, 0));
                }
                else
                {
                    throw std::runtime_error("unsupported array element type in parameter: " + elementType);
                }
            }
            else
            {
                // Check if it's a struct type
                auto structIt = g_struct_types_.find(ptype);
                if (structIt != g_struct_types_.end())
                {
                    paramTypes.push_back(structIt->second);
                }
                else
                {
                    throw std::runtime_error("unsupported parameter type: " + ptype);
                }
            }
        }
        if (verbose_)
            printf("[codegen] creating LLVM function type for %s\n", f->name.c_str());
        LLVMTypeRef ftype = LLVMFunctionType(returnType, paramTypes.empty() ? nullptr : paramTypes.data(),
                                             (unsigned)paramTypes.size(), isVariadic ? 1 : 0); // set variadic flag
        if (verbose_)
            printf("[codegen] adding LLVM function %s to module (variadic: %s)\n", f->name.c_str(), isVariadic ? "yes" : "no");
        LLVMValueRef fn = LLVMAddFunction(module_, f->name.c_str(), ftype);
                        if (f->name != "main")
        {
            LLVMSetLinkage(fn, LLVMInternalLinkage);
        }
        g_function_map_[f->name] = fn;
                g_function_param_types_[f->name] = paramTypes;

                g_variadic_functions_[f->name] = isVariadic;
        if (verbose_ && isVariadic)
        {
            printf("[codegen] registered function '%s' as variadic in g_variadic_functions_\n", f->name.c_str());
        }

                QuarkType returnQuarkType;
        if (f->returnType == "int")
            returnQuarkType = QuarkType::Int;
        else if (f->returnType == "float")
            returnQuarkType = QuarkType::Float;
        else if (f->returnType == "double")
            returnQuarkType = QuarkType::Double;
        else if (f->returnType == "str")
            returnQuarkType = QuarkType::String;
        else if (f->returnType == "bool")
            returnQuarkType = QuarkType::Boolean;
        else if (f->returnType == "void")
            returnQuarkType = QuarkType::Void;
        else
        {
            // Check if it's a struct type
            auto structIt = g_struct_types_.find(f->returnType);
            if (structIt != g_struct_types_.end())
            {
                returnQuarkType = QuarkType::Struct;
            }
            else
            {
                returnQuarkType = QuarkType::Int; // default fallback
            }
        }

                std::string structName = (returnQuarkType == QuarkType::Struct) ? f->returnType : "";
        expressionCodeGen_->declareFunctionType(f->name, returnQuarkType, {}, structName);

        if (verbose_)
            printf("[codegen] successfully declared function %s with return type %s\n", f->name.c_str(), f->returnType.c_str());
    }
}

void CodeGen::generateMainFunction(ProgramAST *program, bool hasUserMain, bool hasTopLevelStmts)
{
    #ifdef _WIN32
    LLVMTypeRef uint32Type = LLVMInt32TypeInContext(llvmCtx_);
    LLVMTypeRef setConsoleOutputCPType = LLVMFunctionType(uint32Type, &uint32Type, 1, 0);
    LLVMValueRef setConsoleOutputCPFn = LLVMAddFunction(module_, "SetConsoleOutputCP", setConsoleOutputCPType);

    LLVMTypeRef setConsoleCPType = LLVMFunctionType(uint32Type, &uint32Type, 1, 0);
    LLVMValueRef setConsoleCPFn = LLVMAddFunction(module_, "SetConsoleCP", setConsoleCPType);

    LLVMValueRef utf8CodePage = LLVMConstInt(uint32Type, 65001, 0);
#endif

    if (hasUserMain)
    {
                        return;
    }
    else if (hasTopLevelStmts)
    {
        LLVMTypeRef mainType = LLVMFunctionType(int32_t_, nullptr, 0, 0);
        LLVMValueRef mainFn = LLVMAddFunction(module_, "main", mainType);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(llvmCtx_, mainFn, "entry");
        LLVMPositionBuilderAtEnd(builder_, bb);

#ifdef _WIN32
                LLVMValueRef utf8CodePage = LLVMConstInt(uint32Type, 65001, 0);
        LLVMBuildCall2(builder_, setConsoleOutputCPType, setConsoleOutputCPFn, &utf8CodePage, 1, "");
        LLVMBuildCall2(builder_, setConsoleCPType, setConsoleCPFn, &utf8CodePage, 1, "");
#endif

        LLVMValueRef putsFn = LLVMGetNamedFunction(module_, "puts");
        for (auto &s : program->stmts)
        {
            if (!dynamic_cast<FunctionAST *>(s.get()))
                statementCodeGen_->genStmt(s.get(), putsFn);
        }

        LLVMBuildRet(builder_, LLVMConstInt(int32_t_, 0, 0));
    }
    else
    {
        LLVMTypeRef mainType = LLVMFunctionType(int32_t_, nullptr, 0, 0);
        LLVMValueRef mainFn = LLVMAddFunction(module_, "main", mainType);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(llvmCtx_, mainFn, "entry");
        LLVMPositionBuilderAtEnd(builder_, bb);
        LLVMBuildRet(builder_, LLVMConstInt(int32_t_, 0, 0));
    }
}

void CodeGen::generate(ProgramAST *program, const std::string &outFile)
{
        if (progress_) {
        std::string displayPath = outFile;
        try {
            std::filesystem::path absPath = std::filesystem::absolute(outFile);
            std::filesystem::path currentPath = std::filesystem::current_path();
            displayPath = std::filesystem::relative(absPath, currentPath).string();
        } catch (...) {
                    }
        progress_->start(displayPath);
        progress_->setStage(CompilationProgress::Stage::PARSING);
    }
    
    g_function_map_.clear();
    g_named_values_.clear();
    g_const_values_.clear();
    g_named_types_.clear();

                builtinFunctions_->registerWithGlobalMap(&g_function_map_, &g_function_param_types_, &g_variadic_functions_);
                if (expressionCodeGen_)
        builtinFunctions_->registerTypesWithExpressionCodeGen(expressionCodeGen_.get());

    if (verbose_)
        printf("[codegen] generating code\n");

        if (progress_) {
        progress_->setStage(CompilationProgress::Stage::CODE_GENERATION);
        progress_->setProgress(0.1f);
    }

    if (verbose_)
        printf("[codegen] about to collect functions\n");

        std::vector<StructDefStmt *> allStructDefs;
    for (auto &s : program->stmts)
    {
        if (verbose_)
            printf("[codegen] processing statement\n");
        statementCodeGen_->collectStructDefs(s.get(), allStructDefs);
    }

        if (verbose_)
        printf("[codegen] processing %zu struct definitions\n", allStructDefs.size());
    for (auto *structDef : allStructDefs)
    {
        statementCodeGen_->processStructDef(structDef);
    }

        std::vector<FunctionAST *> allFunctions;
    std::vector<ExternFunctionAST *> allExternFunctions;
    for (auto &s : program->stmts)
    {
        if (verbose_)
            printf("[codegen] processing statement\n");
                statementCodeGen_->predeclareExternStructs(s.get());
        statementCodeGen_->collectFunctions(s.get(), allFunctions);
        statementCodeGen_->collectExternFunctions(s.get(), allExternFunctions);
    }

    if (verbose_)
        printf("[codegen] collected %zu functions and %zu extern functions\n",
               allFunctions.size(), allExternFunctions.size());

    if (verbose_)
        printf("[codegen] declaring LLVM functions\n");

    LLVMValueRef putsFn = LLVMGetNamedFunction(module_, "puts");
    if (verbose_)
        printf("[codegen] declared puts\n");
    LLVMValueRef sprintfFn = LLVMGetNamedFunction(module_, "snprintf");
    if (verbose_)
        printf("[codegen] declared sprintf\n");
    LLVMValueRef freeFn = LLVMGetNamedFunction(module_, "free");
    if (verbose_)
        printf("[codegen] declared free\n");

        for (auto *externFunc : allExternFunctions)
    {
        statementCodeGen_->declareExternFunction(externFunc);
    }

    // Declare user functions
    declareFunctions(allFunctions);

    if (verbose_)
        printf("[codegen] finished declaring all functions, now processing statements\n");

                    for (auto &s : program->stmts)
    {
        if (dynamic_cast<FunctionAST *>(s.get()) || dynamic_cast<IncludeStmt *>(s.get()) || dynamic_cast<ExternFunctionAST *>(s.get()) || dynamic_cast<ImplStmt *>(s.get()) || dynamic_cast<StructDefStmt *>(s.get()))
        {
                        statementCodeGen_->genStmt(s.get(), putsFn);
        }
        else
        {
                                    // point.
            if (verbose_)
                printf("[codegen] deferring top-level statement to main\n");
        }
    }

    if (verbose_)
        printf("[codegen] finished processing all statements\n");

    if (progress_) {
        progress_->setProgress(0.8f);
    }

    // Generate main function
    bool hasUserMain = (g_function_map_.find("main") != g_function_map_.end());
    bool hasTopLevelStmts = false;
    for (auto &s : program->stmts)
        if (!dynamic_cast<FunctionAST *>(s.get()) && !dynamic_cast<ImplStmt *>(s.get()) && !dynamic_cast<IncludeStmt *>(s.get()) && !dynamic_cast<ExternFunctionAST *>(s.get()))
            hasTopLevelStmts = true;

    generateMainFunction(program, hasUserMain, hasTopLevelStmts);

        if (optimize_)
    {
        if (progress_) {
            progress_->setStage(CompilationProgress::Stage::OPTIMIZATION);
        }
        
        if (verbose_)
            printf("[codegen] running optimization passes\n");
        runOptimizationPasses();
    }

        if (progress_) {
        progress_->setStage(CompilationProgress::Stage::LINKING);
        progress_->setProgress(0.1f);
    }
    if (verbose_)
    {
        char *buf = LLVMPrintModuleToString(module_);
        printf("[codegen] module:\n%s\n", buf);
        LLVMDisposeMessage(buf);
        try
        {
            auto cwd = std::filesystem::current_path();
            printf("[codegen] cwd=%s\n", cwd.string().c_str());
        }
        catch (...)
        {
            printf("[codegen] cwd=<unknown>\n");
        }
    }

    if (!emitExecutable(outFile))
    {
        if (progress_) {
            progress_->setError("Failed to emit executable");
            progress_->stop();
        }
                throw EnhancedCodeGenError("failed to emit executable", {/*line*/ 1, /*column*/ 1, /*file*/ "<linker>"}, "", ErrorCodes::CODEGEN_FAILED, 1);
    }
    
    // Complete
    if (progress_) {
        progress_->setStage(CompilationProgress::Stage::COMPLETE);
        progress_->setProgress(1.0f);
        progress_->stop();
    }
}

bool CodeGen::emitExecutable(const std::string &outPath)
{
        LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    // Verify the module first
    char *errorMsg = nullptr;
    if (LLVMVerifyModule(module_, LLVMAbortProcessAction, &errorMsg))
    {
        fprintf(stderr, "[codegen] Module verification failed: %s\n", errorMsg);
        LLVMDisposeMessage(errorMsg);
        return false;
    }

    if (verbose_)
        printf("[codegen] Module verification passed\n");

    if (progress_) {
        progress_->setProgress(0.3f);
    }

        char *triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *tmErr = nullptr;
    if (LLVMGetTargetFromTriple(triple, &target, &tmErr) != 0)
    {
        fprintf(stderr, "[codegen] LLVMGetTargetFromTriple failed: %s\n", tmErr);
        LLVMDisposeMessage(tmErr);
        LLVMDisposeMessage(triple);
        return false;
    }
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target,
        triple,
        "", // CPU
        "", // Features
        LLVMCodeGenLevelDefault,
        LLVMRelocDefault,
        LLVMCodeModelDefault);
    if (!tm)
    {
        fprintf(stderr, "[codegen] Failed to create target machine\n");
        LLVMDisposeMessage(triple);
        return false;
    }
        LLVMSetTarget(module_, triple);
    LLVMDisposeMessage(triple);

    if (progress_) {
        progress_->setProgress(0.5f);
    }

    LLVMMemoryBufferRef objBuf = nullptr;
    if (LLVMTargetMachineEmitToMemoryBuffer(tm, module_, LLVMObjectFile, &tmErr, &objBuf) != 0)
    {
        fprintf(stderr, "[codegen] EmitToMemoryBuffer failed: %s\n", tmErr ? tmErr : "<unknown>");
        if (tmErr)
            LLVMDisposeMessage(tmErr);
        LLVMDisposeTargetMachine(tm);
        return false;
    }
    if (verbose_)
    {
        size_t sz = LLVMGetBufferSize(objBuf);
        printf("[codegen] Emitted object to memory buffer (%zu bytes)\n", sz);
    }

    if (progress_) {
        progress_->setProgress(0.7f);
    }

                        bool lldLinked = false;
    std::string lldErrMsg;
    std::string tmpObjPath;

#ifdef _WIN32
        try
    {
        auto tmpDir = std::filesystem::temp_directory_path();
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::mt19937_64 rng(static_cast<uint64_t>(now));
        uint64_t r = rng();
        char nameBuf[64];
        sprintf_s(nameBuf, "quark_%016llx.obj", static_cast<unsigned long long>(r));
        tmpObjPath = (tmpDir / nameBuf).string();
        FILE *f = fopen(tmpObjPath.c_str(), "wb");
        if (!f)
        {
            fprintf(stderr, "[codegen] Failed to open temp obj for write: %s\n", tmpObjPath.c_str());
        }
        else
        {
            const char *bufPtr = LLVMGetBufferStart(objBuf);
            size_t bufSz = LLVMGetBufferSize(objBuf);
            size_t w = fwrite(bufPtr, 1, bufSz, f);
            fclose(f);
            if (w != bufSz)
            {
                fprintf(stderr, "[codegen] Short write to temp obj (%zu/%zu)\n", w, bufSz);
            }
            else
            {
                if (verbose_)
                    printf("[codegen] Wrote temp obj: %s\n", tmpObjPath.c_str());
                std::string outOpt = "/out:" + outPath;
                                std::string subsystemOpt = "/subsystem:console";

                                                std::vector<std::string> argstrs;
                argstrs.emplace_back("lld-link");
                argstrs.emplace_back("/nologo");
                argstrs.push_back(outOpt);
                argstrs.push_back(subsystemOpt);
                argstrs.push_back(tmpObjPath);

                if (!freestanding_)
                {
                                                            const char *libEnv = std::getenv("LIB");
                    if (libEnv && libEnv[0] != '\0')
                    {
                        std::string libPaths(libEnv);
                        size_t start = 0;
                        while (start < libPaths.size())
                        {
                            size_t pos = libPaths.find(';', start);
                            std::string single = (pos == std::string::npos)
                                                     ? libPaths.substr(start)
                                                     : libPaths.substr(start, pos - start);
                            if (!single.empty())
                                argstrs.emplace_back(std::string("/libpath:") + single);
                            if (pos == std::string::npos)
                                break;
                            start = pos + 1;
                        }
                    }

                                        argstrs.emplace_back("/LIBPATH:C:\\Home\\Data\\vcpkg\\installed\\x64-windows-static-release\\lib");

                                        argstrs.emplace_back("libcmt.lib");                                       argstrs.emplace_back("libvcruntime.lib");                                 argstrs.emplace_back("libucrt.lib");                                      argstrs.emplace_back("legacy_stdio_definitions.lib"); // For printf, scanf, etc.
                    argstrs.emplace_back("libcurl.lib");
                    // Static libcurl dependencies
                    argstrs.emplace_back("zlib.lib");
                    argstrs.emplace_back("zstd.lib");
                                        argstrs.emplace_back("ixwebsocket.lib");
                    argstrs.emplace_back("mbedtls.lib");    // mbedTLS for IXWebSocket SSL
                    argstrs.emplace_back("mbedx509.lib");                       argstrs.emplace_back("mbedcrypto.lib"); // mbedTLS crypto functions
                                        argstrs.emplace_back("kernel32.lib");
                    argstrs.emplace_back("user32.lib");
                    argstrs.emplace_back("gdi32.lib");
                    argstrs.emplace_back("winspool.lib");
                    argstrs.emplace_back("shell32.lib");
                    argstrs.emplace_back("ole32.lib");
                    argstrs.emplace_back("oleaut32.lib");
                    argstrs.emplace_back("uuid.lib");
                    argstrs.emplace_back("comdlg32.lib");
                    argstrs.emplace_back("advapi32.lib");
                    argstrs.emplace_back("ws2_32.lib");
                                        argstrs.emplace_back("crypt32.lib");
                    argstrs.emplace_back("bcrypt.lib");                     argstrs.emplace_back("iphlpapi.lib");
                    argstrs.emplace_back("secur32.lib");
                    argstrs.emplace_back("normaliz.lib");
                                        argstrs.emplace_back("winhttp.lib");
                                        argstrs.emplace_back("userenv.lib"); // For GetUserProfileDirectoryW
                    argstrs.emplace_back("dbghelp.lib");                     argstrs.emplace_back("psapi.lib");   
                                        argstrs.emplace_back("/libpath:build/Release");

                                        argstrs.emplace_back("quark_http.lib");
                    argstrs.emplace_back("quark_json.lib");
                    argstrs.emplace_back("quark_toml.lib");
                    argstrs.emplace_back("quark_ws.lib");
                    argstrs.emplace_back("quark_io.lib");
                                    }
                else
                {
                                        // and avoid default libraries.
                    argstrs.emplace_back("/ENTRY:main");
                    argstrs.emplace_back("/NODEFAULTLIB");
                }

                for (const auto &path : additionalLibraryPaths_)
                {
                    if (!path.empty())
                        argstrs.emplace_back(std::string("/libpath:") + path);
                }

                for (const auto &lib : additionalLibraries_)
                {
                    if (!lib.empty())
                        argstrs.emplace_back(lib);
                }

                std::vector<const char *> args;
                args.reserve(argstrs.size());
                for (auto &s : argstrs)
                    args.push_back(s.c_str());

                llvm::SmallString<0> outBuf, errBuf;
                llvm::raw_svector_ostream outOS(outBuf), errOS(errBuf);
                llvm::ArrayRef<const char *> argv(args.data(), args.size());
                lldLinked = lld::coff::link(argv, outOS, errOS, false, false);
                if (!lldLinked)
                {
                    std::string errStr = std::string(errOS.str().begin(), errOS.str().end());
                    lldErrMsg = errStr;
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove(tmpObjPath, ec);
        if (verbose_ && ec)
        {
            fprintf(stderr, "[codegen] Warning: failed to remove temp obj: %s (ec=%d)\n", tmpObjPath.c_str(), (int)ec.value());
        }
    }
    catch (const std::exception &ex)
    {
        lldErrMsg = ex.what();
    }
#elif (__APPLE__)
    // Mach-O linking for macOS
    try
    {
        auto tmpDir = std::filesystem::temp_directory_path();
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::mt19937_64 rng(static_cast<uint64_t>(now));
        uint64_t r = rng();
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "quark_%016llx.o", static_cast<unsigned long long>(r));
        tmpObjPath = (tmpDir / nameBuf).string();
        FILE *f = fopen(tmpObjPath.c_str(), "wb");
        if (!f)
        {
            fprintf(stderr, "[codegen] Failed to open temp obj for write: %s\n", tmpObjPath.c_str());
        }
        else
        {
            const char *bufPtr = LLVMGetBufferStart(objBuf);
            size_t bufSz = LLVMGetBufferSize(objBuf);
            size_t w = fwrite(bufPtr, 1, bufSz, f);
            fclose(f);
            if (w != bufSz)
            {
                fprintf(stderr, "[codegen] Short write to temp obj (%zu/%zu)\n", w, bufSz);
            }
            else
            {
                if (verbose_)
                    printf("[codegen] Wrote temp obj: %s\n", tmpObjPath.c_str());
                std::vector<std::string> argstrs;
                argstrs.reserve(40);
                argstrs.emplace_back("ld64.lld");
                if (freestanding_)
                {
                                                            argstrs.emplace_back("-nostdlib");
                    argstrs.emplace_back("-e");
                    argstrs.emplace_back("main");
                }
                argstrs.emplace_back("-arch");
                argstrs.emplace_back("arm64");
                argstrs.emplace_back("-platform_version");
                argstrs.emplace_back("macos");
                argstrs.emplace_back("12.0.0");
                argstrs.emplace_back("12.0.0");
                argstrs.emplace_back("-L");
                argstrs.emplace_back("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib");
                argstrs.emplace_back("-L");
                argstrs.emplace_back("/opt/homebrew/lib");
                argstrs.emplace_back("-L");
                argstrs.emplace_back("build");
                argstrs.emplace_back("-syslibroot");
                argstrs.emplace_back("/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk");
                argstrs.emplace_back("-o");
                argstrs.emplace_back(outPath);
                argstrs.emplace_back(tmpObjPath);
                argstrs.emplace_back("build/libquark_http.a");
                argstrs.emplace_back("build/libquark_json.a");
                argstrs.emplace_back("build/libquark_toml.a");
                argstrs.emplace_back("build/libquark_ws.a");
                argstrs.emplace_back("build/libquark_io.a");
                argstrs.emplace_back("-lixwebsocket");
                argstrs.emplace_back("-lcurl");
                argstrs.emplace_back("-lz");
                argstrs.emplace_back("-lzstd");
                argstrs.emplace_back("-framework");
                argstrs.emplace_back("CoreFoundation");
                argstrs.emplace_back("-framework");
                argstrs.emplace_back("CoreServices");
                argstrs.emplace_back("-framework");
                argstrs.emplace_back("Security");
                argstrs.emplace_back("-framework");
                argstrs.emplace_back("CFNetwork");
                argstrs.emplace_back("-framework");
                argstrs.emplace_back("SystemConfiguration");
                argstrs.emplace_back("-lc++");
                argstrs.emplace_back("-lc++abi");
                argstrs.emplace_back("-lc");
                argstrs.emplace_back("-lm");

                for (const auto &path : additionalLibraryPaths_)
                {
                    if (!path.empty())
                    {
                        argstrs.emplace_back("-L");
                        argstrs.emplace_back(path);
                    }
                }

                for (const auto &lib : additionalLibraries_)
                {
                    if (!lib.empty())
                        argstrs.emplace_back(lib);
                }

                std::vector<const char *> args;
                args.reserve(argstrs.size());
                for (auto &str : argstrs)
                    args.push_back(str.c_str());

                llvm::SmallString<0> outBuf, errBuf;
                llvm::raw_svector_ostream outOS(outBuf), errOS(errBuf);
                llvm::ArrayRef<const char *> argv(args.data(), args.size());
                lldLinked = lld::macho::link(argv, outOS, errOS, false, false);
                if (!lldLinked)
                {
                    std::string errStr = std::string(errOS.str().begin(), errOS.str().end());
                    lldErrMsg = errStr;
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove(tmpObjPath, ec);
        if (verbose_ && ec)
        {
            fprintf(stderr, "[codegen] Warning: failed to remove temp obj: %s (ec=%d)\n", tmpObjPath.c_str(), (int)ec.value());
        }
    }
    catch (const std::exception &ex)
    {
        lldErrMsg = ex.what();
    }
#else
    // ELF linking for Linux
    try
    {
        auto tmpDir = std::filesystem::temp_directory_path();
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::mt19937_64 rng(static_cast<uint64_t>(now));
        uint64_t r = rng();
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "quark_%016llx.o", static_cast<unsigned long long>(r));
        tmpObjPath = (tmpDir / nameBuf).string();
        FILE *f = fopen(tmpObjPath.c_str(), "wb");
        if (!f)
        {
            fprintf(stderr, "[codegen] Failed to open temp obj for write: %s\n", tmpObjPath.c_str());
        }
        else
        {
            const char *bufPtr = LLVMGetBufferStart(objBuf);
            size_t bufSz = LLVMGetBufferSize(objBuf);
            size_t w = fwrite(bufPtr, 1, bufSz, f);
            fclose(f);
            if (w != bufSz)
            {
                fprintf(stderr, "[codegen] Short write to temp obj (%zu/%zu)\n", w, bufSz);
            }
            else
            {
                if (verbose_)
                    printf("[codegen] Wrote temp obj: %s\n", tmpObjPath.c_str());
                // Prepare lld ELF args
                std::vector<std::string> argstrs;
                argstrs.reserve(16);
                argstrs.emplace_back("ld.lld");
                if (freestanding_)
                {
                                                            argstrs.emplace_back("-nostdlib");
                    argstrs.emplace_back("-e");
                    argstrs.emplace_back("main");
                }
                argstrs.emplace_back("-o");
                argstrs.emplace_back(outPath);
                argstrs.emplace_back(tmpObjPath);
                argstrs.emplace_back("build/libquark_io.a");
                argstrs.emplace_back("build/libquark_toml.a");
                argstrs.emplace_back("-lc");
                argstrs.emplace_back("-lm");

                for (const auto &path : additionalLibraryPaths_)
                {
                    if (!path.empty())
                    {
                        argstrs.emplace_back("-L");
                        argstrs.emplace_back(path);
                    }
                }

                for (const auto &lib : additionalLibraries_)
                {
                    if (!lib.empty())
                        argstrs.emplace_back(lib);
                }

                std::vector<const char *> args;
                args.reserve(argstrs.size());
                for (auto &s : argstrs)
                    args.push_back(s.c_str());

                llvm::SmallString<0> outBuf, errBuf;
                llvm::raw_svector_ostream outOS(outBuf), errOS(errBuf);
                llvm::ArrayRef<const char *> argv(args.data(), args.size());
                lldLinked = lld::elf::link(argv, outOS, errOS, false, false);
                if (!lldLinked)
                {
                    std::string errStr = std::string(errOS.str().begin(), errOS.str().end());
                    lldErrMsg = errStr;
                }
            }
        }
        std::error_code ec;
        std::filesystem::remove(tmpObjPath, ec);
        if (verbose_ && ec)
        {
            fprintf(stderr, "[codegen] Warning: failed to remove temp obj: %s (ec=%d)\n", tmpObjPath.c_str(), (int)ec.value());
        }
    }
    catch (const std::exception &ex)
    {
        lldErrMsg = ex.what();
    }
#endif

        LLVMDisposeMemoryBuffer(objBuf);
    LLVMDisposeTargetMachine(tm);

    if (lldLinked)
    {
        if (verbose_)
            printf("[codegen] Linked executable via LLD: %s\n", outPath.c_str());
        return true;
    }
        if (!lldErrMsg.empty())
    {
        throw EnhancedCodeGenError(std::string("linker failed: ") + lldErrMsg, {/*line*/ 1, /*col*/ 1, /*file*/ outPath}, "", ErrorCodes::SYMBOL_NOT_FOUND, 1);
    }
    return false;
}

void CodeGen::runOptimizationPasses()
{
    if (verbose_)
        printf("[codegen] setting up modern LLVM optimization passes (level O%d)\n", optimizationLevel_);

        llvm::LLVMContext *cppCtx = llvm::unwrap(llvmCtx_);
    llvm::Module *cppModule = llvm::unwrap(module_);

            if (cppModule->getTargetTriple().empty())
    {
        char *cTriple = LLVMGetDefaultTargetTriple();
        if (cTriple)
        {
            LLVMSetTarget(module_, cTriple);
            LLVMDisposeMessage(cTriple);
        }
    }

        llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

        llvm::PassBuilder PB;

        PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        llvm::OptimizationLevel optLevel;
    switch (optimizationLevel_)
    {
    case 0:
        optLevel = llvm::OptimizationLevel::O0;
        break;
    case 1:
        optLevel = llvm::OptimizationLevel::O1;
        break;
    case 2:
        optLevel = llvm::OptimizationLevel::O2;
        break;
    case 3:
        optLevel = llvm::OptimizationLevel::O3;
        break;
    default:
        optLevel = llvm::OptimizationLevel::O2;
        break;
    }

        if (verbose_)
        printf("[codegen] building optimization pipeline (O%d level)\n", optimizationLevel_);

    llvm::ModulePassManager MPM;

            if (verbose_)
        printf("[codegen] internalizing non-exported symbols\n");
    MPM.addPass(llvm::InternalizePass([&](const llvm::GlobalValue &GV)
                                      {
        if (GV.isDeclaration()) return true;         return GV.getName() == "main"; }));

        llvm::ModulePassManager DefaultMPM = PB.buildPerModuleDefaultPipeline(optLevel);

    // Add the default passes
    MPM.addPass(std::move(DefaultMPM));

        if (optimizationLevel_ > 0)
    {
        if (verbose_)
            printf("[codegen] adding explicit dead code elimination passes\n");

                MPM.addPass(llvm::GlobalDCEPass());

                MPM.addPass(llvm::DeadArgumentEliminationPass());

                MPM.addPass(llvm::StripDeadPrototypesPass());

                        MPM.addPass(llvm::GlobalOptPass());

                llvm::FunctionPassManager FPM;
        FPM.addPass(llvm::DCEPass());  // Basic dead code elimination
        FPM.addPass(llvm::ADCEPass()); 
                MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));

        // Final global cleanup pass
        MPM.addPass(llvm::GlobalDCEPass());
    }

    // Run the optimization passes
    if (verbose_)
        printf("[codegen] running optimization passes on module\n");

    MPM.run(*cppModule, MAM);

    if (verbose_)
        printf("[codegen] modern optimization passes completed\n");
}

std::vector<uint8_t> CodeGen::emitBitcode()
{
    if (!module_) {
        return {};
    }
    
    LLVMMemoryBufferRef memBuf = LLVMWriteBitcodeToMemoryBuffer(module_);
    if (!memBuf) {
        if (verbose_)
            printf("[codegen] failed to write bitcode to memory buffer\n");
        return {};
    }
    
    const char* data = LLVMGetBufferStart(memBuf);
    size_t size = LLVMGetBufferSize(memBuf);
    
    std::vector<uint8_t> result(data, data + size);
    LLVMDisposeMemoryBuffer(memBuf);
    
    if (verbose_)
        printf("[codegen] emitted %zu bytes of bitcode\n", result.size());
    
    return result;
}

bool CodeGen::loadBitcode(const std::vector<uint8_t>& bitcode)
{
    if (bitcode.empty()) {
        return false;
    }
    
    LLVMMemoryBufferRef memBuf = LLVMCreateMemoryBufferWithMemoryRangeCopy(
        reinterpret_cast<const char*>(bitcode.data()),
        bitcode.size(),
        "cached_module"
    );
    
    if (!memBuf) {
        if (verbose_)
            printf("[codegen] failed to create memory buffer from bitcode\n");
        return false;
    }
    
    LLVMModuleRef loadedModule = nullptr;
    char* errorMsg = nullptr;
    
    if (LLVMParseBitcodeInContext2(llvmCtx_, memBuf, &loadedModule) != 0) {
        if (verbose_)
            printf("[codegen] failed to parse bitcode: %s\n", errorMsg ? errorMsg : "unknown error");
        LLVMDisposeMessage(errorMsg);
        return false;
    }
    
    if (module_) {
        LLVMDisposeModule(module_);
    }
    module_ = loadedModule;
    
    if (verbose_)
        printf("[codegen] loaded module from bitcode\n");
    
    return true;
}

bool CodeGen::loadBitcodeFromFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        if (verbose_)
            printf("[codegen] failed to open bitcode file: %s\n", path.c_str());
        return false;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> bitcode(size);
    if (!file.read(reinterpret_cast<char*>(bitcode.data()), size)) {
        if (verbose_)
            printf("[codegen] failed to read bitcode file: %s\n", path.c_str());
        return false;
    }
    
    return loadBitcode(bitcode);
}
