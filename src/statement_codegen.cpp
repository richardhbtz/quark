#include "../include/statement_codegen.h"
#include "../include/compilation_context.h"
#include "../include/error_reporter.h"
#include "../include/source_manager.h"
#include "../include/codegen_types.h"
#include <stdexcept>
#include <cstdio>
#include <vector>
#include <string>
#include <memory>
#include <functional>

StatementCodeGen::StatementCodeGen(LLVMContextRef ctx, LLVMModuleRef module, LLVMBuilderRef builder,
                                   ExternalFunctions *externalFunctions, ExpressionCodeGen *expressionCodeGen,
                                   bool verbose, CompilationContext *compilationCtx)
    : ctx_(ctx), module_(module), builder_(builder), externalFunctions_(externalFunctions),
      expressionCodeGen_(expressionCodeGen), verbose_(verbose), ctx_compilation_(compilationCtx)
{
    int32_t_ = LLVMInt32TypeInContext(ctx_);
    int8ptr_t_ = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
    bool_t_ = LLVMInt1TypeInContext(ctx_);
    float_t_ = LLVMFloatTypeInContext(ctx_);
    double_t_ = LLVMDoubleTypeInContext(ctx_);

    g_function_map_ = nullptr;
    g_named_values_ = nullptr;
    g_const_values_ = nullptr;
    g_function_param_types_ = nullptr;
    g_named_types_ = nullptr;
}

ErrorReporter *StatementCodeGen::errorReporter() const
{
    if (ctx_compilation_)
        return &ctx_compilation_->errorReporter;
    return g_errorReporter.get();
}

SourceManager *StatementCodeGen::sourceManager() const
{
    if (ctx_compilation_)
        return &ctx_compilation_->sourceManager;
    return g_sourceManager.get();
}

void StatementCodeGen::setGlobalSymbolTables(std::unordered_map<std::string, LLVMValueRef> *functionMap,
                                             std::unordered_map<std::string, LLVMValueRef> *namedValues,
                                             std::unordered_map<std::string, double> *constValues,
                                             std::unordered_map<std::string, std::vector<LLVMTypeRef>> *functionParamTypes,
                                             std::unordered_map<std::string, LLVMTypeRef> *namedTypes,
                                             std::unordered_map<std::string, StructDefStmt *> *structDefs,
                                             std::unordered_map<std::string, LLVMTypeRef> *structTypes,
                                             std::unordered_map<std::string, bool> *variadicFunctions)
{
    g_function_map_ = functionMap;
    g_named_values_ = namedValues;
    g_const_values_ = constValues;
    g_function_param_types_ = functionParamTypes;
    g_named_types_ = namedTypes;
    g_struct_defs_ = structDefs;
    g_struct_types_ = structTypes;
    g_variadic_functions_ = variadicFunctions;
}

void StatementCodeGen::genStmt(StmtAST *stmt, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] genStmt called with stmt type\n");

    if (!stmt)
    {
        if (verbose_)
            printf("[codegen] genStmt received null statement\n");
        return;
    }

    if (auto *inc = dynamic_cast<IncludeStmt *>(stmt))
    {
        genIncludeStmt(inc, putsFn);
        return;
    }

    // Module declaration - no code generation needed
    if (auto *modDecl = dynamic_cast<ModuleDeclStmt *>(stmt))
    {
        if (verbose_)
            printf("[codegen] Module declaration: %s (no codegen needed)\n", modDecl->moduleName.c_str());
        return;
    }

    if (auto *externFunc = dynamic_cast<ExternFunctionAST *>(stmt))
    {
        if (!g_function_map_ || g_function_map_->find(externFunc->name) == g_function_map_->end())
        {
            declareExternFunction(externFunc);
        }
        return;
    }

    if (auto *externStruct = dynamic_cast<ExternStructDeclAST *>(stmt))
    {
        if (!g_struct_types_)
            return;
        if (g_struct_types_->find(externStruct->name) == g_struct_types_->end())
        {
            LLVMTypeRef opaque = LLVMStructCreateNamed(ctx_, externStruct->name.c_str());
            (*g_struct_types_)[externStruct->name] = opaque;
        }
        return;
    }

    // Handle function definitions
    if (auto *f = dynamic_cast<FunctionAST *>(stmt))
    {
        genFunctionStmt(f, putsFn);
        return;
    }

    if (auto *vdecl = dynamic_cast<VarDeclStmt *>(stmt))
    {
        genVarDeclStmt(vdecl);
        return;
    }

    if (auto *as = dynamic_cast<AssignStmtAST *>(stmt))
    {
        genAssignStmt(as);
        return;
    }

    // Handle if statements
    if (auto *ifs = dynamic_cast<IfStmtAST *>(stmt))
    {
        genIfStmt(ifs, putsFn);
        return;
    }

    // Handle for statements
    if (auto *forStmt = dynamic_cast<ForStmt *>(stmt))
    {
        genForStmt(forStmt, putsFn);
        return;
    }

    // Handle while statements
    if (auto *whileStmt = dynamic_cast<WhileStmt *>(stmt))
    {
        genWhileStmt(whileStmt, putsFn);
        return;
    }

    // Handle break/continue
    if (dynamic_cast<BreakStmt *>(stmt))
    {
        genBreakStmt();
        return;
    }
    if (dynamic_cast<ContinueStmt *>(stmt))
    {
        genContinueStmt();
        return;
    }

    // Handle struct definitions
    if (auto *structDef = dynamic_cast<StructDefStmt *>(stmt))
    {
        genStructDefStmt(structDef);
        return;
    }

    // Handle impl blocks
    if (auto *impl = dynamic_cast<ImplStmt *>(stmt))
    {
        genImplStmt(impl, putsFn);
        return;
    }

    // Handle return statements
    if (auto *retStmt = dynamic_cast<ReturnStmt *>(stmt))
    {
        genReturnStmt(retStmt);
        return;
    }

    // Handle member assignments
    if (auto *memberAssign = dynamic_cast<MemberAssignStmt *>(stmt))
    {
        genMemberAssignStmt(memberAssign);
        return;
    }

    if (auto *derefAssign = dynamic_cast<DerefAssignStmt *>(stmt))
    {
        genDerefAssignStmt(derefAssign);
        return;
    }

    if (auto *arrayAssign = dynamic_cast<ArrayAssignStmt *>(stmt))
    {
        genArrayAssignStmt(arrayAssign);
        return;
    }

    // Handle match statements
    if (auto *matchStmt = dynamic_cast<MatchStmt *>(stmt))
    {
        genMatchStmt(matchStmt);
        return;
    }

    // Handle expression statements
    if (auto *es = dynamic_cast<ExprStmtAST *>(stmt))
    {
        genExprStmt(es, putsFn);
        return;
    }

    if (verbose_)
        printf("[codegen] unknown statement type - skipping\n");
}

void StatementCodeGen::genIncludeStmt(IncludeStmt *inc, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] processing include statement with %zu stmts\n", inc->stmts.size());
    for (auto &s : inc->stmts)
    {
        genStmt(s.get(), putsFn);
    }
    if (verbose_)
        printf("[codegen] finished processing include statement\n");
}

void StatementCodeGen::genFunctionStmt(FunctionAST *f, LLVMValueRef putsFn)
{
    if (!g_function_map_)
        throw std::runtime_error("global symbol tables not initialized");

    std::string funcName = f->name;
    if (f->isExtension)
    {
        funcName = f->extensionType + "::" + f->name;
    }

    if (verbose_)
        printf("[codegen] generating body for function: %s with %zu body statements\n", funcName.c_str(), f->body.size());

    auto it = g_function_map_->find(funcName);
    if (it == g_function_map_->end())
    {
        if (verbose_)
            printf("[codegen] function '%s' not found in function map\n", funcName.c_str());
        throw std::runtime_error("function '" + funcName + "' not declared");
    }

    LLVMValueRef fn = it->second;
    if (LLVMCountBasicBlocks(fn) > 0)
    {
        if (verbose_)
            printf("[codegen] function '%s' already has a body; skipping generation\n", funcName.c_str());
        return;
    }

    currentFunctionName_ = funcName;
    currentFunctionReturnType_ = f->returnType;

    std::string structName = "";
    bool isStructMethod = false;
    bool isConstructor = false;
    bool isExtensionMethod = f->isExtension;
    size_t colonPos = funcName.find("::");
    if (colonPos != std::string::npos && !isExtensionMethod)
    {
        structName = funcName.substr(0, colonPos);
        std::string methodName = funcName.substr(colonPos + 2);
        isStructMethod = true;
        isConstructor = (methodName == "new" && f->returnType == structName);
        if (verbose_)
        {
            printf("[codegen] detected struct method for struct: %s, method: %s, isConstructor: %s\n",
                   structName.c_str(), methodName.c_str(), isConstructor ? "true" : "false");
        }
    }

    LLVMBasicBlockRef savedInsertBB = LLVMGetInsertBlock(builder_);
    if (verbose_)
        printf("[codegen] creating basic block for %s\n", funcName.c_str());
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(ctx_, fn, "entry");
    LLVMPositionBuilderAtEnd(builder_, bb);

    // Save current variable state
    auto savedNamedValues = *g_named_values_;
    auto savedNamedTypes = *g_named_types_;

    if (verbose_)
        printf("[codegen] clearing variables for function scope\n");

    // Save globals (extern variables) before clearing - they have LLVMGlobalValueKind
    std::unordered_map<std::string, LLVMValueRef> savedGlobalVars;
    std::unordered_map<std::string, LLVMTypeRef> savedGlobalTypes;
    for (const auto &pair : *g_named_values_)
    {
        if (LLVMIsAGlobalVariable(pair.second))
        {
            savedGlobalVars[pair.first] = pair.second;
            auto typeIt = g_named_types_->find(pair.first);
            if (typeIt != g_named_types_->end())
            {
                savedGlobalTypes[pair.first] = typeIt->second;
            }
        }
    }

    g_named_values_->clear();
    g_named_types_->clear();

    // Restore global/extern variables
    for (const auto &pair : savedGlobalVars)
    {
        (*g_named_values_)[pair.first] = pair.second;
    }
    for (const auto &pair : savedGlobalTypes)
    {
        (*g_named_types_)[pair.first] = pair.second;
    }

    if (verbose_)
        printf("[codegen] setting up %zu parameters\n", f->params.size());

    std::unordered_map<std::string, bool> functionParams;

    unsigned int paramIndex = 0;
    if (isStructMethod && !isConstructor)
    {
        LLVMValueRef thisArg = LLVMGetParam(fn, paramIndex++);
        LLVMSetValueName(thisArg, "this_ptr");

        auto structIt = g_struct_types_->find(structName);
        if (structIt != g_struct_types_->end())
        {
            LLVMTypeRef selfPtrTy = LLVMPointerType(structIt->second, 0);
            LLVMValueRef thisAlloca = LLVMBuildAlloca(builder_, selfPtrTy, "this_ptr.alloca");
            LLVMBuildStore(builder_, thisArg, thisAlloca);
            (*g_named_values_)["this"] = thisAlloca;
            (*g_named_types_)["this"] = selfPtrTy;
            functionParams["this"] = false; // alloca-based
            expressionCodeGen_->declareVariable("this", QuarkType::Struct, SourceLocation(), structName);

            auto structDefIt = g_struct_defs_->find(structName);
            if (structDefIt != g_struct_defs_->end())
            {
                StructDefStmt *structDef = structDefIt->second;
                for (const auto &field : structDef->fields)
                {
                    const std::string &fieldName = field.first;
                    const std::string &fieldType = field.second;

                    if (verbose_)
                        printf("[codegen] making field '%s' available in method scope\n", fieldName.c_str());

                    expressionCodeGen_->registerStructField(fieldName, structName, fieldType);
                }
            }
        }

        LLVMValueRef dynNameArg = LLVMGetParam(fn, paramIndex++);
        LLVMSetValueName(dynNameArg, "__dyn_type_name");
        LLVMValueRef dynAlloca = LLVMBuildAlloca(builder_, int8ptr_t_, "__dyn_type_name.alloca");
        LLVMBuildStore(builder_, dynNameArg, dynAlloca);
        (*g_named_values_)["__dyn_type_name"] = dynAlloca;
        (*g_named_types_)["__dyn_type_name"] = int8ptr_t_;
        functionParams["__dyn_type_name"] = false;
    }

    unsigned llvmParamIndex = paramIndex;
    for (unsigned i = 0; i < f->params.size(); ++i)
    {
        const std::string &pname = f->params[i].first;
        const std::string &ptype = f->params[i].second;

        if (pname == "..." && ptype == "...")
        {
            if (verbose_)
                printf("[codegen] skipping variadic parameter marker\n");
            continue;
        }

        LLVMValueRef arg = LLVMGetParam(fn, llvmParamIndex);
        if (verbose_)
            printf("[codegen] param %d (LLVM %d): %s:%s\n", i, llvmParamIndex, pname.c_str(), ptype.c_str());
        LLVMSetValueName(arg, pname.c_str());
        llvmParamIndex++;
        LLVMTypeRef paramType;
        if (ptype == "int")
            paramType = LLVMInt32TypeInContext(ctx_);
        else if (ptype == "float")
            paramType = float_t_;
        else if (ptype == "double")
            paramType = double_t_;
        else if (ptype == "str")
            paramType = int8ptr_t_;
        else if (ptype == "bool")
            paramType = bool_t_;
        else if (!ptype.empty() && ptype.find('*') != std::string::npos)
        {
            paramType = expressionCodeGen_->mapPointerType(ptype);
        }
        else if (ptype.size() > 2 && ptype.substr(ptype.size() - 2) == "[]")
        {
            std::string elementType = ptype.substr(0, ptype.size() - 2);
            if (elementType == "str")
            {
                paramType = LLVMPointerType(int8ptr_t_, 0);
            }
            else if (elementType == "int")
            {
                paramType = LLVMPointerType(LLVMInt32TypeInContext(ctx_), 0);
            }
            else if (elementType == "float")
            {
                paramType = LLVMPointerType(float_t_, 0);
            }
            else if (elementType == "double")
            {
                paramType = LLVMPointerType(double_t_, 0);
            }
            else if (elementType == "bool")
            {
                paramType = LLVMPointerType(bool_t_, 0);
            }
            else
            {
                paramType = LLVMInt32TypeInContext(ctx_); // default fallback
            }
        }
        else
        {
            // Check if it's a struct type
            auto structIt = g_struct_types_->find(ptype);
            if (structIt != g_struct_types_->end())
            {
                paramType = structIt->second;
            }
            else
            {
                paramType = LLVMInt32TypeInContext(ctx_); // default fallback
            }
        }

        LLVMValueRef paramAlloca = LLVMBuildAlloca(builder_, paramType, pname.c_str());
        LLVMBuildStore(builder_, arg, paramAlloca);

        (*g_named_values_)[pname] = paramAlloca;
        functionParams[pname] = false;
        if (verbose_)
        {
            LLVMTypeRef allocaTy = LLVMTypeOf(paramAlloca);
            LLVMTypeRef argTy = LLVMTypeOf(arg);
            int allocaKind = (int)LLVMGetTypeKind(allocaTy);
            int argKind = (int)LLVMGetTypeKind(argTy);
            printf("[prologue] param '%s': alloca type kind=%d, arg type kind=%d\n", pname.c_str(), allocaKind, argKind);
            if (pname == "msg")
            {
                printf("[prologue] detailed param 'msg': allocaTy==argTy? %d\n", (int)(allocaTy == argTy));
            }
            char *allocaTypeStr = LLVMPrintTypeToString(allocaTy);
            char *argTypeStr = LLVMPrintTypeToString(argTy);
            char *allocaValStr = LLVMPrintValueToString(paramAlloca);
            char *argValStr = LLVMPrintValueToString(arg);
            printf("[prologue] param '%s' types: alloca='%s' arg='%s'\n", pname.c_str(), allocaTypeStr, argTypeStr);
            printf("[prologue] param '%s' values: alloca_val='%s' arg_val='%s'\n", pname.c_str(), allocaValStr, argValStr);
            LLVMDisposeMessage(allocaTypeStr);
            LLVMDisposeMessage(argTypeStr);
            LLVMDisposeMessage(allocaValStr);
            LLVMDisposeMessage(argValStr);
        }

        if (ptype == "int")
            (*g_named_types_)[pname] = LLVMInt32TypeInContext(ctx_);
        else if (ptype == "float")
            (*g_named_types_)[pname] = float_t_;
        else if (ptype == "double")
            (*g_named_types_)[pname] = double_t_;
        else if (ptype == "str")
            (*g_named_types_)[pname] = int8ptr_t_;
        else if (ptype == "bool")
            (*g_named_types_)[pname] = bool_t_;
        else if (!ptype.empty() && ptype.find('*') != std::string::npos)
        {
            (*g_named_types_)[pname] = expressionCodeGen_->mapPointerType(ptype);
            expressionCodeGen_->declareVariable(pname, QuarkType::Pointer, SourceLocation(), "", ptype);
        }
        else if (ptype.size() > 2 && ptype.substr(ptype.size() - 2) == "[]")
        {
            std::string elementType = ptype.substr(0, ptype.size() - 2);
            if (elementType == "str")
            {
                (*g_named_types_)[pname] = LLVMPointerType(int8ptr_t_, 0);
            }
            else if (elementType == "int")
            {
                (*g_named_types_)[pname] = LLVMPointerType(LLVMInt32TypeInContext(ctx_), 0);
            }
            else if (elementType == "float")
            {
                (*g_named_types_)[pname] = LLVMPointerType(float_t_, 0);
            }
            else if (elementType == "double")
            {
                (*g_named_types_)[pname] = LLVMPointerType(double_t_, 0);
            }
            else if (elementType == "bool")
            {
                (*g_named_types_)[pname] = LLVMPointerType(bool_t_, 0);
            }
            QuarkType elementQuarkType = QuarkType::Unknown;
            if (elementType == "str")
                elementQuarkType = QuarkType::String;
            else if (elementType == "int")
                elementQuarkType = QuarkType::Int;
            else if (elementType == "float")
                elementQuarkType = QuarkType::Float;
            else if (elementType == "double")
                elementQuarkType = QuarkType::Double;
            else if (elementType == "bool")
                elementQuarkType = QuarkType::Boolean;
            if (elementQuarkType != QuarkType::Unknown)
            {
                expressionCodeGen_->declareVariable(pname, QuarkType::Array, SourceLocation(), elementQuarkType, 0);
            }
        }
        else
        {
            // Check if it's a struct type
            auto structIt = g_struct_types_->find(ptype);
            if (structIt != g_struct_types_->end())
            {
                (*g_named_types_)[pname] = structIt->second;
                expressionCodeGen_->declareVariable(pname, QuarkType::Struct, SourceLocation(), ptype);
            }
        }
    }

    expressionCodeGen_->setFunctionParameters(functionParams);

    // Generate function body
    if (verbose_)
        printf("[codegen] generating function body with %zu statements\n", f->body.size());
    for (size_t i = 0; i < f->body.size(); ++i)
    {
        if (verbose_)
            printf("[codegen] processing body statement %zu/%zu\n", i + 1, f->body.size());
        try
        {
            genStmt(f->body[i].get(), putsFn);
            if (verbose_)
                printf("[codegen] completed body statement %zu\n", i + 1);
        }
        catch (const std::exception &e)
        {
            if (verbose_)
                printf("[codegen] error in body statement %zu: %s\n", i + 1, e.what());
            throw;
        }
    }

    if (verbose_)
        printf("[codegen] ensuring function '%s' has terminators on all paths\n", f->name.c_str());

    // Create epilogue block
    LLVMBasicBlockRef epilogueBB = LLVMAppendBasicBlockInContext(ctx_, fn, "func_epilogue");

    for (LLVMBasicBlockRef bbIt = LLVMGetFirstBasicBlock(fn); bbIt; bbIt = LLVMGetNextBasicBlock(bbIt))
    {
        if (bbIt == epilogueBB)
            continue; // skip epilogue itself
        if (!LLVMGetBasicBlockTerminator(bbIt))
        {
            LLVMPositionBuilderAtEnd(builder_, bbIt);
            LLVMBuildBr(builder_, epilogueBB);
        }
    }

    LLVMPositionBuilderAtEnd(builder_, epilogueBB);
    if (f->returnType == "void")
    {
        LLVMBuildRetVoid(builder_);
    }
    else if (f->returnType == "int")
    {
        LLVMBuildRet(builder_, LLVMConstInt(int32_t_, 0, 0));
    }
    else if (f->returnType == "float")
    {
        LLVMBuildRet(builder_, LLVMConstReal(float_t_, 0.0));
    }
    else if (f->returnType == "double")
    {
        LLVMBuildRet(builder_, LLVMConstReal(double_t_, 0.0));
    }
    else if (f->returnType == "str")
    {
        LLVMValueRef emptyStr = LLVMBuildGlobalStringPtr(builder_, "", "empty_str");
        LLVMBuildRet(builder_, emptyStr);
    }
    else if (f->returnType == "bool")
    {
        LLVMBuildRet(builder_, LLVMConstInt(bool_t_, 0, 0));
    }
    else
    {
        auto structIt = g_struct_types_->find(f->returnType);
        if (structIt != g_struct_types_->end())
        {
            LLVMValueRef zeroStruct = LLVMConstNull(structIt->second);
            LLVMBuildRet(builder_, zeroStruct);
        }
        else
        {
            LLVMBuildRet(builder_, LLVMConstInt(int32_t_, 0, 0));
        }
    }

    if (verbose_)
        printf("[codegen] restoring variable state\n");
    *g_named_values_ = savedNamedValues;
    *g_named_types_ = savedNamedTypes;

    expressionCodeGen_->clearFunctionParameters();

    if (isStructMethod && !isConstructor)
    {
        expressionCodeGen_->clearStructFields();
    }

    currentFunctionName_.clear();
    currentFunctionReturnType_.clear();

    if (savedInsertBB)
    {
        LLVMPositionBuilderAtEnd(builder_, savedInsertBB);
    }

    if (verbose_)
        printf("[codegen] completed function %s\n", f->name.c_str());
}

void StatementCodeGen::genVarDeclStmt(VarDeclStmt *vdecl)
{
    if (!g_named_values_ || !g_named_types_)
        throw std::runtime_error("global symbol tables not initialized");

    if (verbose_)
        printf("[codegen] processing variable declaration: %s %s\n", vdecl->type.c_str(), vdecl->name.c_str());

    // Helper to throw enhanced errors with proper location
    auto throwError = [this, vdecl](const std::string &msg, const SourceLocation &loc, const std::string &errorCode, int length = 1)
    {
        auto *sm = sourceManager();
        if (sm)
        {
            auto file = sm->getFile(loc.filename);
            if (file)
            {
                throw EnhancedCodeGenError(msg, loc, file->content, errorCode, length);
            }
        }
        throw std::runtime_error(msg);
    };

    TypeInfo initType;
    try
    {
        initType = expressionCodeGen_->inferType(vdecl->init.get());
    }
    catch (const EnhancedCodeGenError &)
    {
        throw; // Re-throw enhanced errors as-is
    }
    catch (const std::exception &e)
    {
        throwError("Failed to infer type for variable '" + vdecl->name +
                       "' initializer: " + std::string(e.what()),
                   vdecl->init ? vdecl->init->location : vdecl->location,
                   ErrorCodes::INVALID_TYPE, (int)vdecl->name.size());
    }

    QuarkType declaredType;
    std::shared_ptr<FunctionPointerTypeInfo> inferredFpInfo = nullptr;
    if (vdecl->type == "auto")
    {
        declaredType = initType.type;
        if (initType.type == QuarkType::FunctionPointer)
        {
            // Preserve function pointer info from inferred type
            inferredFpInfo = initType.funcPtrInfo;
        }
        if (verbose_)
            printf("[codegen] inferred type for %s: %d\n", vdecl->name.c_str(), (int)declaredType);
    }
    else if (vdecl->type == "str")
    {
        declaredType = QuarkType::String;
    }
    else if (vdecl->type == "bool")
    {
        declaredType = QuarkType::Boolean;
    }
    else if (IntegerTypeUtils::isFunctionPointerType(vdecl->type))
    {
        // Function pointer type: fn(args) -> ret
        declaredType = QuarkType::FunctionPointer;
    }
    else if (!vdecl->type.empty() && vdecl->type.back() == '*')
    {
        declaredType = QuarkType::Pointer;
    }
    else if (vdecl->type.size() > 2 && vdecl->type.substr(vdecl->type.size() - 2) == "[]")
    {
        declaredType = QuarkType::Array;
    }
    else
    {
        // Try integer family first
        declaredType = IntegerTypeUtils::stringToQuarkType(vdecl->type);
        if (declaredType == QuarkType::Unknown)
        {
            // Check if it's a struct type
            if (g_struct_defs_ && g_struct_defs_->find(vdecl->type) != g_struct_defs_->end())
            {
                declaredType = QuarkType::Struct;
            }
            else
            {
                throwError("Unknown type '" + vdecl->type + "' in variable declaration",
                           vdecl->location, ErrorCodes::INVALID_TYPE, (int)vdecl->type.size());
            }
        }
    }

    if (vdecl->type != "auto")
    {
        if (initType.type != QuarkType::Unknown && declaredType != initType.type)
        {
            bool bothIntegers = IntegerTypeUtils::isIntegerType(declaredType) && IntegerTypeUtils::isIntegerType(initType.type);
            bool bothNumeric = IntegerTypeUtils::isNumericType(declaredType) && IntegerTypeUtils::isNumericType(initType.type);
            // Allow FunctionPointer to be assigned to void* (Pointer)
            bool fnPtrToVoidPtr = (initType.type == QuarkType::FunctionPointer &&
                                   declaredType == QuarkType::Pointer &&
                                   vdecl->type == "void*");
            // Allow null to be assigned to void* (Pointer)
            bool nullToVoidPtr = (initType.type == QuarkType::Null &&
                                  declaredType == QuarkType::Pointer);
            // Allow array literals to initialize list variables
            bool arrayToList = (initType.type == QuarkType::Array &&
                                declaredType == QuarkType::List);
            if (!bothIntegers && !bothNumeric && !fnPtrToVoidPtr && !nullToVoidPtr && !arrayToList)
            {
                auto typeToStr = [](QuarkType t)
                {
                    if (IntegerTypeUtils::isIntegerType(t))
                        return std::string("integer");
                    if (IntegerTypeUtils::isFloatingType(t))
                    {
                        if (t == QuarkType::Float)
                            return std::string("float");
                        if (t == QuarkType::Double)
                            return std::string("double");
                    }
                    return IntegerTypeUtils::quarkTypeToString(t);
                };
                std::string declaredStr = (declaredType == QuarkType::Struct) ? ("struct " + vdecl->type) : typeToStr(declaredType);
                std::string actualStr = (initType.type == QuarkType::Struct) ? ("struct " + initType.structName) : typeToStr(initType.type);
                throwError("Type mismatch in declaration of '" + vdecl->name +
                               "': declared as " + declaredStr + " but initialized with " + actualStr,
                           vdecl->init->location, ErrorCodes::TYPE_MISMATCH, (int)vdecl->name.size());
            }
        }
    }

    LLVMTypeRef varType;
    LLVMValueRef val = nullptr;

    std::string actualType;
    switch (declaredType)
    {
    case QuarkType::String:
        actualType = "str";
        varType = int8ptr_t_;
        {
            // Check if we're at global scope (no current basic block)
            LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);
            if (!currentBB)
            {
                // Global string variable - must be initialized with a literal
                if (auto *strExpr = dynamic_cast<StringExprAST *>(vdecl->init.get()))
                {
                    // Create the string constant first to get the properly typed array
                    unsigned strLen = static_cast<unsigned int>(strExpr->value.length() + 1);
                    LLVMValueRef initVal = LLVMConstString(strExpr->value.c_str(), strLen - 1, 0);
                    LLVMTypeRef strArrayType = LLVMTypeOf(initVal); // Get the actual array type from the constant

                    std::string dataName = vdecl->name + "_data";

                    LLVMValueRef globalStrData = LLVMAddGlobal(module_, strArrayType, dataName.c_str());
                    LLVMSetInitializer(globalStrData, initVal);
                    LLVMSetLinkage(globalStrData, LLVMPrivateLinkage);
                    LLVMSetGlobalConstant(globalStrData, 1); // Mark as constant

                    // Store the data global directly - when accessed, it acts as a char*
                    (*g_named_values_)[vdecl->name] = globalStrData;
                    (*g_named_types_)[vdecl->name] = strArrayType;
                    expressionCodeGen_->declareVariable(vdecl->name, declaredType, vdecl->init->location);

                    if (verbose_)
                        printf("[codegen] completed global string variable declaration: %s\n", vdecl->name.c_str());
                    return; // Early return for global string
                }
                else
                {
                    throw std::runtime_error("only string literals supported for global string variables");
                }
            }
        }
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        break;
    case QuarkType::Boolean:
        actualType = "bool";
        varType = bool_t_;
        val = expressionCodeGen_->genExprBool(vdecl->init.get());
        break;
    case QuarkType::Int:
        actualType = "int";
        varType = int32_t_;
        val = expressionCodeGen_->genExprInt(vdecl->init.get());
        break;
    case QuarkType::Map:
        actualType = "map";
        varType = int8ptr_t_;
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        break;
    case QuarkType::List:
        actualType = "list";
        varType = int8ptr_t_;
        // Check if init is an array literal and convert it to list
        if (auto *arrayLit = dynamic_cast<ArrayLiteralExpr *>(vdecl->init.get()))
        {
            // Convert to ListLiteralExpr
            ListLiteralExpr listLit;
            listLit.location = arrayLit->location;
            for (auto &elem : arrayLit->elements)
            {
                listLit.elements.push_back(std::move(elem));
            }
            val = expressionCodeGen_->genListLiteral(&listLit);
        }
        else
        {
            val = expressionCodeGen_->genExpr(vdecl->init.get());
        }
        break;
    case QuarkType::Float:
        actualType = "float";
        varType = float_t_;
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        if (val)
        {
            LLVMTypeRef vty = LLVMTypeOf(val);
            if (LLVMGetTypeKind(vty) == LLVMDoubleTypeKind)
            {
                // double -> float
                val = LLVMBuildFPTrunc(builder_, val, float_t_, "trunc_to_float");
            }
            else if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind)
            {
                // int -> float
                val = LLVMBuildSIToFP(builder_, val, float_t_, "i_to_float");
            }
        }
        break;
    case QuarkType::Double:
        actualType = "double";
        varType = double_t_;
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        if (val)
        {
            LLVMTypeRef vty = LLVMTypeOf(val);
            if (LLVMGetTypeKind(vty) == LLVMFloatTypeKind)
            {
                // float -> double
                val = LLVMBuildFPExt(builder_, val, double_t_, "ext_to_double");
            }
            else if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind)
            {
                // int -> double
                val = LLVMBuildSIToFP(builder_, val, double_t_, "i_to_double");
            }
        }
        break;
    case QuarkType::Void:
        throwError("Variables cannot have type void", vdecl->location, ErrorCodes::INVALID_TYPE, 4);
    case QuarkType::Struct:
        if (vdecl->type == "auto")
        {
            actualType = initType.structName;
        }
        else
        {
            actualType = vdecl->type;
        }
        if (g_struct_types_)
        {
            auto structTypeIt = g_struct_types_->find(actualType);
            if (structTypeIt != g_struct_types_->end())
            {
                varType = structTypeIt->second;
                val = expressionCodeGen_->genExpr(vdecl->init.get());
            }
            else
            {
                throwError("Struct type not found: " + actualType, vdecl->location, ErrorCodes::SYMBOL_NOT_FOUND, (int)actualType.size());
            }
        }
        else
        {
            throwError("Struct types not initialized", vdecl->location, ErrorCodes::CODEGEN_FAILED, 1);
        }
        break;
    case QuarkType::Pointer:
    {
        actualType = vdecl->type;
        varType = expressionCodeGen_->mapPointerType(vdecl->type);
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        break;
    }
    case QuarkType::Array:
        actualType = vdecl->type;
        if (initType.elementType == QuarkType::Int)
        {
            varType = LLVMPointerType(int32_t_, 0);
        }
        else if (initType.elementType == QuarkType::Boolean)
        {
            varType = LLVMPointerType(bool_t_, 0);
        }
        else if (initType.elementType == QuarkType::String)
        {
            varType = LLVMPointerType(int8ptr_t_, 0);
        }
        else
        {
            varType = int8ptr_t_;
        }
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        break;
    case QuarkType::Null:
        // Null literal - treat as void pointer (i8*)
        varType = int8ptr_t_;
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        break;
    case QuarkType::FunctionPointer:
    {
        // Function pointer type - stored as void* (i8*)
        actualType = vdecl->type;
        varType = int8ptr_t_; // Function pointers are just void*
        val = expressionCodeGen_->genExpr(vdecl->init.get());

        std::shared_ptr<FunctionPointerTypeInfo> fpInfo;

        // If we inferred the type from the initializer, use that info
        if (inferredFpInfo)
        {
            fpInfo = inferredFpInfo;
        }
        else
        {
            // Parse the function pointer type to extract signature info
            fpInfo = std::make_shared<FunctionPointerTypeInfo>();
            // Parse format: fn(int, str) -> int
            std::string typeStr = vdecl->type;
            if (typeStr.size() > 2 && typeStr.substr(0, 2) == "fn" && typeStr[2] == '(')
            {
                size_t parenClose = typeStr.find(')');
                if (parenClose != std::string::npos)
                {
                    // Extract parameter types
                    std::string paramsStr = typeStr.substr(3, parenClose - 3);
                    if (!paramsStr.empty())
                    {
                        size_t pos = 0, lastPos = 0;
                        while ((pos = paramsStr.find(',', lastPos)) != std::string::npos)
                        {
                            std::string param = paramsStr.substr(lastPos, pos - lastPos);
                            // Trim whitespace
                            size_t start = param.find_first_not_of(" \t");
                            size_t end = param.find_last_not_of(" \t");
                            if (start != std::string::npos && end != std::string::npos)
                            {
                                fpInfo->paramTypes.push_back(param.substr(start, end - start + 1));
                            }
                            lastPos = pos + 1;
                        }
                        // Last parameter
                        std::string param = paramsStr.substr(lastPos);
                        size_t start = param.find_first_not_of(" \t");
                        size_t end = param.find_last_not_of(" \t");
                        if (start != std::string::npos && end != std::string::npos)
                        {
                            fpInfo->paramTypes.push_back(param.substr(start, end - start + 1));
                        }
                    }

                    // Extract return type (after "->")
                    size_t arrowPos = typeStr.find("->", parenClose);
                    if (arrowPos != std::string::npos)
                    {
                        std::string retStr = typeStr.substr(arrowPos + 2);
                        size_t start = retStr.find_first_not_of(" \t");
                        size_t end = retStr.find_last_not_of(" \t");
                        if (start != std::string::npos && end != std::string::npos)
                        {
                            fpInfo->returnType = retStr.substr(start, end - start + 1);
                        }
                    }
                    else
                    {
                        fpInfo->returnType = "void";
                    }
                }
            }
        }

        // Declare the variable with function pointer type info
        std::string typeStr = (vdecl->type == "auto" && fpInfo) ? fpInfo->toString() : vdecl->type;
        TypeInfo fpTypeInfo(QuarkType::FunctionPointer, vdecl->location, "", QuarkType::Unknown, 0, typeStr, fpInfo);
        expressionCodeGen_->declareVariable(vdecl->name, fpTypeInfo);
        break;
    }
    case QuarkType::Char:
        actualType = "char";
        varType = LLVMInt8TypeInContext(ctx_);
        val = expressionCodeGen_->genExpr(vdecl->init.get());
        // Handle conversion from int to char if needed
        if (val)
        {
            LLVMTypeRef vty = LLVMTypeOf(val);
            if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vty) > 8)
            {
                val = LLVMBuildTrunc(builder_, val, LLVMInt8TypeInContext(ctx_), "trunc_to_char");
            }
        }
        break;
    default:
        throwError("Unsupported variable type for " + vdecl->name, vdecl->location, ErrorCodes::INVALID_TYPE, (int)vdecl->name.size());
    }

    LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);
    LLVMValueRef storage;

    if (currentBB)
    {
        // Create alloca in entry block to avoid stack growth in loops
        LLVMValueRef currentFn = LLVMGetBasicBlockParent(currentBB);
        storage = createEntryBlockAlloca(currentFn, varType, vdecl->name.c_str());

        if (declaredType == QuarkType::Struct)
        {
            if (auto *callExpr = dynamic_cast<CallExprAST *>(vdecl->init.get()))
            {
                if (verbose_)
                    printf("[codegen] storing struct value from function call\n");
                LLVMBuildStore(builder_, val, storage);
            }
            else if (auto *staticCallExpr = dynamic_cast<StaticCallExpr *>(vdecl->init.get()))
            {
                if (verbose_)
                    printf("[codegen] storing struct value from static method call\n");
                LLVMBuildStore(builder_, val, storage);
            }
            else
            {
                if (verbose_)
                    printf("[codegen] copying struct from pointer\n");
                LLVMValueRef structSize = LLVMSizeOf(varType);
                LLVMValueRef dst = LLVMBuildBitCast(builder_, storage, LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0), "dst_cast");
                LLVMValueRef src = LLVMBuildBitCast(builder_, val, LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0), "src_cast");

                auto defIt = g_struct_defs_->find(actualType);
                if (defIt != g_struct_defs_->end())
                {
                    const StructDefStmt *structDef = defIt->second;
                    for (size_t i = 0; i < structDef->fields.size(); i++)
                    {
                        LLVMValueRef srcIndices[2] = {
                            LLVMConstInt(LLVMInt32Type(), 0, 0),
                            LLVMConstInt(LLVMInt32Type(), i, 0)};
                        LLVMValueRef dstIndices[2] = {
                            LLVMConstInt(LLVMInt32Type(), 0, 0),
                            LLVMConstInt(LLVMInt32Type(), i, 0)};
                        LLVMValueRef srcField = LLVMBuildGEP2(builder_, varType, val, srcIndices, 2, "src_field");
                        LLVMValueRef dstField = LLVMBuildGEP2(builder_, varType, storage, dstIndices, 2, "dst_field");

                        const std::string &fieldTypeName = structDef->fields[i].second;
                        LLVMTypeRef fieldType;
                        if (fieldTypeName == "str")
                        {
                            fieldType = int8ptr_t_;
                        }
                        else if (fieldTypeName == "int")
                        {
                            fieldType = int32_t_;
                        }
                        else if (fieldTypeName == "bool")
                        {
                            fieldType = bool_t_;
                        }
                        else if (fieldTypeName.size() > 2 && fieldTypeName.substr(fieldTypeName.size() - 2) == "[]")
                        {
                            std::string elementType = fieldTypeName.substr(0, fieldTypeName.size() - 2);
                            if (elementType == "str")
                            {
                                fieldType = LLVMPointerType(int8ptr_t_, 0);
                            }
                            else if (elementType == "int")
                            {
                                fieldType = LLVMPointerType(int32_t_, 0);
                            }
                            else if (elementType == "bool")
                            {
                                fieldType = LLVMPointerType(bool_t_, 0);
                            }
                            else
                            {
                                throw std::runtime_error("unknown array element type: " + elementType);
                            }
                        }
                        else if (!fieldTypeName.empty() && fieldTypeName.back() == '*')
                        {
                            // Pointer-typed field
                            std::string base = fieldTypeName.substr(0, fieldTypeName.size() - 1);
                            if (base == "void" || base == "char" || base == "str")
                            {
                                fieldType = LLVMPointerType(int8ptr_t_, 0);
                            }
                            else if (base == "int")
                            {
                                fieldType = LLVMPointerType(int32_t_, 0);
                            }
                            else if (base == "float")
                            {
                                fieldType = LLVMPointerType(LLVMFloatTypeInContext(ctx_), 0);
                            }
                            else if (base == "double")
                            {
                                fieldType = LLVMPointerType(LLVMDoubleTypeInContext(ctx_), 0);
                            }
                            else if (base == "bool")
                            {
                                fieldType = LLVMPointerType(bool_t_, 0);
                            }
                            else
                            {
                                auto sit = g_struct_types_->find(base);
                                if (sit != g_struct_types_->end())
                                {
                                    fieldType = LLVMPointerType(sit->second, 0);
                                }
                                else
                                {
                                    // Fallback: opaque i8*
                                    fieldType = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
                                }
                            }
                        }
                        else
                        {
                            // It's a struct type
                            auto structFieldTypeIt = g_struct_types_->find(fieldTypeName);
                            if (structFieldTypeIt != g_struct_types_->end())
                            {
                                fieldType = structFieldTypeIt->second;
                            }
                            else
                            {
                                throw std::runtime_error("unknown field type: " + fieldTypeName);
                            }
                        }

                        LLVMValueRef fieldValue = LLVMBuildLoad2(builder_, fieldType, srcField, "field_val");
                        LLVMBuildStore(builder_, fieldValue, dstField);
                    }
                }
                else
                {
                    throw std::runtime_error("struct definition not found for copying: " + actualType);
                }
            }
        }
        else
        {
            LLVMBuildStore(builder_, val, storage);
        }
    }
    else
    {
        if (declaredType == QuarkType::String)
        {
            if (auto *strExpr = dynamic_cast<StringExprAST *>(vdecl->init.get()))
            {
                LLVMValueRef globalStrData = LLVMAddGlobal(module_,
                                                           LLVMArrayType(LLVMInt8TypeInContext(ctx_), static_cast<unsigned int>(strExpr->value.length() + 1)),
                                                           (vdecl->name + "_data").c_str());
                LLVMSetInitializer(globalStrData, LLVMConstString(strExpr->value.c_str(), static_cast<unsigned int>(strExpr->value.length()), 0));
                LLVMSetLinkage(globalStrData, LLVMPrivateLinkage);

                storage = LLVMAddGlobal(module_, int8ptr_t_, vdecl->name.c_str());
                LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0);
                LLVMValueRef indices[] = {zero, zero};
                LLVMValueRef strPtr = LLVMConstGEP2(LLVMArrayType(LLVMInt8TypeInContext(ctx_), static_cast<unsigned int>(strExpr->value.length() + 1)),
                                                    globalStrData, indices, 2);
                LLVMSetInitializer(storage, strPtr);
            }
            else
            {
                throw std::runtime_error("only string literals supported for global string variables");
            }
        }
        else
        {
            storage = LLVMAddGlobal(module_, varType, vdecl->name.c_str());
            LLVMSetInitializer(storage, val);
        }
    }

    (*g_named_values_)[vdecl->name] = storage;
    (*g_named_types_)[vdecl->name] = varType;

    if (declaredType == QuarkType::Struct)
    {
        std::string structName = (vdecl->type == "auto") ? initType.structName : vdecl->type;
        expressionCodeGen_->declareVariable(vdecl->name, declaredType, vdecl->init->location, structName);
    }
    else if (declaredType == QuarkType::Array)
    {
        expressionCodeGen_->declareVariable(vdecl->name, declaredType, vdecl->init->location, initType.elementType, initType.arraySize);
    }
    else if (declaredType == QuarkType::Pointer)
    {
        std::string pointerTypeName = (vdecl->type == "auto") ? initType.pointerTypeName : vdecl->type;
        expressionCodeGen_->declareVariable(vdecl->name, declaredType, vdecl->init->location, "", pointerTypeName);
    }
    else if (declaredType == QuarkType::Null)
    {
        // Null is treated as a pointer type (void*)
        expressionCodeGen_->declareVariable(vdecl->name, QuarkType::Pointer, vdecl->init->location, "", "void*");
    }
    else
    {
        expressionCodeGen_->declareVariable(vdecl->name, declaredType, vdecl->init->location);
    }

    if (verbose_)
        printf("[codegen] completed variable declaration: %s\n", vdecl->name.c_str());
}

void StatementCodeGen::genAssignStmt(AssignStmtAST *as)
{
    if (!g_named_values_ || !g_named_types_)
        throw std::runtime_error("global symbol tables not initialized");

    if (verbose_)
        printf("[codegen] processing assignment to: %s\n", as->varName.c_str());

    if (!as->value)
    {
        if (verbose_)
            printf("[codegen] assignment has null value\n");
        return;
    }

    TypeInfo valueType;
    try
    {
        valueType = expressionCodeGen_->inferType(as->value.get());
    }
    catch (const std::exception &e)
    {
        if (verbose_)
            printf("[codegen] type inference failed: %s\n", e.what());
        valueType = TypeInfo(QuarkType::Unknown, as->value->location);
    }

    auto it = g_named_values_->find(as->varName);
    if (it == g_named_values_->end())
    {
        LLVMTypeRef varType;
        LLVMValueRef val = nullptr;
        QuarkType qType = valueType.type;
        std::string pointerTypeNameForValue;

        switch (qType)
        {
        case QuarkType::Int:
            varType = int32_t_;
            val = expressionCodeGen_->genExprInt(as->value.get());
            break;
        case QuarkType::Float:
            varType = float_t_;
            val = expressionCodeGen_->genExpr(as->value.get());
            // Normalize to float storage
            if (val)
            {
                LLVMTypeRef vty = LLVMTypeOf(val);
                if (LLVMGetTypeKind(vty) == LLVMDoubleTypeKind)
                {
                    val = LLVMBuildFPTrunc(builder_, val, varType, "trunc_to_float");
                }
                else if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind)
                {
                    val = LLVMBuildSIToFP(builder_, val, varType, "i_to_float");
                }
            }
            break;
        case QuarkType::Double:
            varType = double_t_;
            val = expressionCodeGen_->genExpr(as->value.get());
            // Normalize to double storage
            if (val)
            {
                LLVMTypeRef vty = LLVMTypeOf(val);
                if (LLVMGetTypeKind(vty) == LLVMFloatTypeKind)
                {
                    val = LLVMBuildFPExt(builder_, val, varType, "ext_to_double");
                }
                else if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind)
                {
                    val = LLVMBuildSIToFP(builder_, val, varType, "i_to_double");
                }
            }
            break;
        case QuarkType::String:
            varType = int8ptr_t_;
            val = expressionCodeGen_->genExpr(as->value.get());
            break;
        case QuarkType::Boolean:
            varType = bool_t_;
            val = expressionCodeGen_->genExprBool(as->value.get());
            break;
        case QuarkType::Pointer:
            pointerTypeNameForValue = !valueType.pointerTypeName.empty() ? valueType.pointerTypeName : std::string("void*");
            varType = expressionCodeGen_->mapPointerType(pointerTypeNameForValue);
            val = expressionCodeGen_->genExpr(as->value.get());
            break;
        default:
            if (verbose_)
                printf("[codegen] cannot determine type for assignment, defaulting to string\n");
            varType = int8ptr_t_;
            val = expressionCodeGen_->genExpr(as->value.get());
            break;
        }

        LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);
        LLVMValueRef storage;

        if (currentBB)
        {
            // Create alloca in entry block to avoid stack growth in loops
            LLVMValueRef currentFn = LLVMGetBasicBlockParent(currentBB);
            storage = createEntryBlockAlloca(currentFn, varType, as->varName.c_str());
            LLVMBuildStore(builder_, val, storage);
        }
        else
        {
            if (qType == QuarkType::String)
            {
                if (auto *strExpr = dynamic_cast<StringExprAST *>(as->value.get()))
                {
                    LLVMValueRef globalStrData = LLVMAddGlobal(module_, LLVMArrayType(LLVMInt8TypeInContext(ctx_), static_cast<unsigned int>(strExpr->value.length() + 1)),
                                                               (as->varName + "_data").c_str());
                    LLVMSetInitializer(globalStrData, LLVMConstString(strExpr->value.c_str(), static_cast<unsigned int>(strExpr->value.length()), 0));
                    LLVMSetLinkage(globalStrData, LLVMPrivateLinkage);

                    storage = LLVMAddGlobal(module_, int8ptr_t_, as->varName.c_str());
                    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0);
                    LLVMValueRef indices[] = {zero, zero};
                    LLVMValueRef strPtr = LLVMConstGEP2(LLVMArrayType(LLVMInt8TypeInContext(ctx_), static_cast<unsigned int>(strExpr->value.length() + 1)),
                                                        globalStrData, indices, 2);
                    LLVMSetInitializer(storage, strPtr);
                }
                else
                {
                    throw std::runtime_error("only string literals supported for global string variables");
                }
            }
            else
            {
                storage = LLVMAddGlobal(module_, varType, as->varName.c_str());
                LLVMSetInitializer(storage, val);
            }
        }

        (*g_named_values_)[as->varName] = storage;
        (*g_named_types_)[as->varName] = varType;
        if (qType == QuarkType::Pointer)
        {
            if (pointerTypeNameForValue.empty())
                pointerTypeNameForValue = valueType.pointerTypeName;
            if (pointerTypeNameForValue.empty())
                pointerTypeNameForValue = "void*";
            expressionCodeGen_->declareVariable(as->varName, qType, as->value->location, "", pointerTypeNameForValue);
        }
        else
        {
            expressionCodeGen_->declareVariable(as->varName, qType, as->value->location);
        }
    }
    else
    {
        QuarkType existingType = expressionCodeGen_->getVariableType(as->varName);

        if (existingType != QuarkType::Unknown && valueType.type != QuarkType::Unknown)
        {
            if (existingType != valueType.type)
            {
                bool bothIntegers = IntegerTypeUtils::isIntegerType(existingType) && IntegerTypeUtils::isIntegerType(valueType.type);
                // Allow FunctionPointer to be assigned to void* (Pointer)
                bool fnPtrToVoidPtr = (valueType.type == QuarkType::FunctionPointer &&
                                       existingType == QuarkType::Pointer);
                if (!bothIntegers && !fnPtrToVoidPtr)
                {
                    auto typeToStr = [](QuarkType t)
                    {
                        if (IntegerTypeUtils::isIntegerType(t))
                            return std::string("integer");
                        return IntegerTypeUtils::quarkTypeToString(t);
                    };
                    std::string expectedStr = typeToStr(existingType);
                    std::string actualStr = typeToStr(valueType.type);
                    throw std::runtime_error("Type mismatch in assignment to '" + as->varName +
                                             "': expected " + expectedStr + " but got " + actualStr +
                                             " at line " + std::to_string(as->value->location.line));
                }
            }
        }

        LLVMValueRef val = nullptr;
        auto typeIt = g_named_types_->find(as->varName);
        if (typeIt != g_named_types_->end())
        {
            LLVMTypeRef targetTy = typeIt->second;
            if (targetTy == int8ptr_t_)
            {
                // string
                val = expressionCodeGen_->genExpr(as->value.get());
            }
            else if (targetTy == bool_t_)
            {
                // bool
                val = expressionCodeGen_->genExprBool(as->value.get());
            }
            else if (LLVMGetTypeKind(targetTy) == LLVMIntegerTypeKind)
            {
                // integer (respect width)
                val = expressionCodeGen_->genExprIntWithType(as->value.get(), targetTy);
            }
            else if (LLVMGetTypeKind(targetTy) == LLVMFloatTypeKind)
            {
                // float
                val = expressionCodeGen_->genExpr(as->value.get());
                LLVMTypeRef vty = val ? LLVMTypeOf(val) : nullptr;
                if (val)
                {
                    if (LLVMGetTypeKind(vty) == LLVMDoubleTypeKind)
                    {
                        val = LLVMBuildFPTrunc(builder_, val, targetTy, "trunc_to_float");
                    }
                    else if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind)
                    {
                        val = LLVMBuildSIToFP(builder_, val, targetTy, "i_to_float");
                    }
                    else if (LLVMGetTypeKind(vty) != LLVMFloatTypeKind)
                    {
                        // Best-effort cast
                        val = LLVMBuildBitCast(builder_, val, targetTy, "to_float");
                    }
                }
            }
            else if (LLVMGetTypeKind(targetTy) == LLVMDoubleTypeKind)
            {
                // double
                val = expressionCodeGen_->genExpr(as->value.get());
                LLVMTypeRef vty = val ? LLVMTypeOf(val) : nullptr;
                if (val)
                {
                    if (LLVMGetTypeKind(vty) == LLVMFloatTypeKind)
                    {
                        val = LLVMBuildFPExt(builder_, val, targetTy, "ext_to_double");
                    }
                    else if (LLVMGetTypeKind(vty) == LLVMIntegerTypeKind)
                    {
                        val = LLVMBuildSIToFP(builder_, val, targetTy, "i_to_double");
                    }
                    else if (LLVMGetTypeKind(vty) != LLVMDoubleTypeKind)
                    {
                        // Best-effort cast
                        val = LLVMBuildBitCast(builder_, val, targetTy, "to_double");
                    }
                }
            }
            else
            {
                // Fallback
                val = expressionCodeGen_->genExpr(as->value.get());
            }
        }
        else
        {
            val = expressionCodeGen_->genExpr(as->value.get());
        }
        LLVMBuildStore(builder_, val, it->second);
    }
    if (verbose_)
        printf("[codegen] completed assignment to: %s\n", as->varName.c_str());
}

void StatementCodeGen::genExprStmt(ExprStmtAST *es, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] processing expression statement\n");

    if (!es->expr)
    {
        if (verbose_)
            printf("[codegen] expression statement has null expression\n");
        return;
    }

    try
    {
        if (auto *call = dynamic_cast<CallExprAST *>(es->expr.get()))
        {
            if (verbose_)
                printf("[codegen] expression statement is a function call: %s\n", call->callee.c_str());
            try
            {
                (void)expressionCodeGen_->genExpr(es->expr.get());
            }
            catch (const std::exception &e)
            {
                if (verbose_)
                    printf("[codegen] failed to generate call expression: %s\n", e.what());
                return;
            }
        }
        else if (auto *var = dynamic_cast<VariableExprAST *>(es->expr.get()))
        {
            if (verbose_)
                printf("[codegen] expression statement is a variable reference: %s\n", var->name.c_str());

            if (g_named_values_)
            {
                auto it = g_named_values_->find(var->name);
                if (it == g_named_values_->end())
                {
                    if (verbose_)
                        printf("[codegen] undefined variable in expression statement: %s\n", var->name.c_str());
                    // Skip instead of throwing
                    return;
                }
            }
        }
        else if (auto *binary = dynamic_cast<BinaryExprAST *>(es->expr.get()))
        {
            if (verbose_)
                printf("[codegen] expression statement is a binary expression\n");

            try
            {
                expressionCodeGen_->genExpr(es->expr.get());
            }
            catch (const std::exception &e)
            {
                if (verbose_)
                    printf("[codegen] failed to generate binary expression: %s\n", e.what());
                return;
            }
        }
        else if (auto *num = dynamic_cast<NumberExprAST *>(es->expr.get()))
        {
            if (verbose_)
                printf("[codegen] expression statement is a number literal\n");
            // No code generation needed
        }
        else if (auto *str = dynamic_cast<StringExprAST *>(es->expr.get()))
        {
            if (verbose_)
                printf("[codegen] expression statement is a string literal\n");
            // No code generation needed
        }
        else if (auto *boolean = dynamic_cast<BoolExprAST *>(es->expr.get()))
        {
            if (verbose_)
                printf("[codegen] expression statement is a boolean literal\n");
            // No code generation needed
        }
        else
        {
            if (verbose_)
                printf("[codegen] expression statement is unknown type, trying generic generation\n");

            try
            {
                expressionCodeGen_->genExpr(es->expr.get());
            }
            catch (const std::exception &e)
            {
                if (verbose_)
                    printf("[codegen] failed to generate unknown expression: %s\n", e.what());
                return;
            }
        }

        if (verbose_)
            printf("[codegen] completed expression statement\n");
    }
    catch (const std::exception &e)
    {
        if (verbose_)
            printf("[codegen] expression statement failed: %s\n", e.what());
    }
}

void StatementCodeGen::genIfStmt(IfStmtAST *ifstmt, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] processing if statement\n");

    LLVMValueRef condVal = expressionCodeGen_->genExprBool(ifstmt->cond.get());

    // Current function
    LLVMValueRef currentFn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
    if (!currentFn)
    {
        throw std::runtime_error("if statement generated outside function context");
    }

    // Create blocks
    LLVMBasicBlockRef thenBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "then");
    LLVMBasicBlockRef elseBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "else");
    LLVMBasicBlockRef contBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "ifcont");

    LLVMBuildCondBr(builder_, condVal, thenBB, elseBB);

    // Then block
    LLVMPositionBuilderAtEnd(builder_, thenBB);
    for (size_t i = 0; i < ifstmt->thenBody.size(); ++i)
    {
        genStmt(ifstmt->thenBody[i].get(), putsFn);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder_)))
    {
        LLVMBuildBr(builder_, contBB);
    }

    // Else / elif handling
    LLVMPositionBuilderAtEnd(builder_, elseBB);
    bool emittedElse = false;
    if (!ifstmt->elifs.empty())
    {
        auto &firstElif = ifstmt->elifs[0];
        LLVMValueRef elifCond = expressionCodeGen_->genExprBool(firstElif.first.get());
        LLVMBasicBlockRef elifThenBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "elif_then");
        LLVMBasicBlockRef elifElseBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "elif_else");
        LLVMBuildCondBr(builder_, elifCond, elifThenBB, elifElseBB);
        LLVMPositionBuilderAtEnd(builder_, elifThenBB);
        for (size_t i = 0; i < firstElif.second.size(); ++i)
            genStmt(firstElif.second[i].get(), putsFn);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder_)))
            LLVMBuildBr(builder_, contBB);
        LLVMPositionBuilderAtEnd(builder_, elifElseBB);
        emittedElse = true;
    }

    // Else body
    for (size_t i = 0; i < ifstmt->elseBody.size(); ++i)
    {
        genStmt(ifstmt->elseBody[i].get(), putsFn);
        emittedElse = true;
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder_)))
        LLVMBuildBr(builder_, contBB);

    // Continue block
    LLVMPositionBuilderAtEnd(builder_, contBB);

    if (verbose_)
        printf("[codegen] completed if statement\n");
}

void StatementCodeGen::collectFunctions(StmtAST *stmt, std::vector<FunctionAST *> &functions)
{
    if (verbose_)
        printf("[DEBUG] collectFunctions: called with statement type\n");

    if (verbose_)
        printf("[codegen] collectFunctions called\n");

    if (auto *f = dynamic_cast<FunctionAST *>(stmt))
    {
        if (verbose_)
            printf("[DEBUG] collectFunctions: found regular function: %s\n", f->name.c_str());
        if (verbose_)
            printf("[codegen] found function: %s\n", f->name.c_str());
        functions.push_back(f);
    }
    else if (auto *structDef = dynamic_cast<StructDefStmt *>(stmt))
    {
        if (verbose_)
            printf("[DEBUG] collectFunctions: found struct definition %s with %zu methods\n", structDef->name.c_str(), structDef->methods.size());

        if (verbose_)
            printf("[codegen] found struct definition %s with %zu methods\n", structDef->name.c_str(), structDef->methods.size());

        for (auto &method : structDef->methods)
        {
            std::string originalName = method->name;
            method->name = structDef->name + "::" + originalName;
            if (verbose_)
                printf("[DEBUG] collectFunctions: adding method %s to functions vector\n", method->name.c_str());
            if (verbose_)
                printf("[codegen] collecting struct method: %s (originally %s)\n", method->name.c_str(), originalName.c_str());

            functions.push_back(method.get());
        }
    }
    else if (auto *impl = dynamic_cast<ImplStmt *>(stmt))
    {
        if (verbose_)
            printf("[DEBUG] collectFunctions: found impl statement\n");
        if (verbose_)
            printf("[codegen] found impl block for %s with %zu methods\n", impl->structName.c_str(), impl->methods.size());
        for (auto &method : impl->methods)
        {
            if (verbose_)
                printf("[codegen] collecting method: %s\n", method->name.c_str());
            functions.push_back(method.get());
        }
    }
    else if (auto *inc = dynamic_cast<IncludeStmt *>(stmt))
    {
        if (verbose_)
            printf("[DEBUG] collectFunctions: found include statement\n");
        if (verbose_)
            printf("[codegen] found include statement with %zu statements\n", inc->stmts.size());
        for (auto &s : inc->stmts)
        {
            collectFunctions(s.get(), functions);
        }
    }
    else
    {
        if (verbose_)
            printf("[DEBUG] collectFunctions: statement is not a function, struct, impl, or include\n");
        if (verbose_)
            printf("[codegen] statement is not a function or include\n");
    }
}

void StatementCodeGen::collectExternFunctions(StmtAST *stmt, std::vector<ExternFunctionAST *> &externFunctions)
{
    if (verbose_)
        printf("[codegen] collectExternFunctions called\n");

    if (auto *externFunc = dynamic_cast<ExternFunctionAST *>(stmt))
    {
        if (verbose_)
            printf("[codegen] found extern function: %s\n", externFunc->name.c_str());
        externFunctions.push_back(externFunc);
    }
    else if (auto *inc = dynamic_cast<IncludeStmt *>(stmt))
    {
        if (verbose_)
            printf("[codegen] found include statement with %zu statements\n", inc->stmts.size());
        for (auto &s : inc->stmts)
        {
            collectExternFunctions(s.get(), externFunctions);
        }
    }
    else
    {
        if (verbose_)
            printf("[codegen] statement is not an extern function or include\n");
    }
}

void StatementCodeGen::predeclareExternStructs(StmtAST *stmt)
{
    if (!stmt)
        return;
    if (auto *es = dynamic_cast<ExternStructDeclAST *>(stmt))
    {
        if (g_struct_types_ && g_struct_types_->find(es->name) == g_struct_types_->end())
        {
            LLVMTypeRef opaque = LLVMStructCreateNamed(ctx_, es->name.c_str());
            (*g_struct_types_)[es->name] = opaque;
        }
        return;
    }
    if (auto *inc = dynamic_cast<IncludeStmt *>(stmt))
    {
        for (auto &s : inc->stmts)
            predeclareExternStructs(s.get());
        return;
    }
}

void StatementCodeGen::declareExternFunction(ExternFunctionAST *externFunc)
{
    if (!g_function_map_ || !g_function_param_types_)
        throw std::runtime_error("global symbol tables not initialized");

    // Check if already declared
    if (g_function_map_->find(externFunc->name) != g_function_map_->end())
    {
        if (verbose_)
            printf("[codegen] extern function '%s' already declared, skipping\n", externFunc->name.c_str());
        return;
    }

    if (verbose_)
        printf("[codegen] declaring extern function: %s with return type %s\n",
               externFunc->name.c_str(), externFunc->returnType.c_str());

    auto mapType = [this](const std::string &type) -> LLVMTypeRef
    {
        if (type.size() > 2 && type.substr(type.size() - 2) == "[]")
        {
            std::string elem = type.substr(0, type.size() - 2);
            if (elem == "int")
                return LLVMPointerType(int32_t_, 0);
            if (elem == "float")
                return LLVMPointerType(float_t_, 0);
            if (elem == "double")
                return LLVMPointerType(double_t_, 0);
            if (elem == "str")
                return LLVMPointerType(int8ptr_t_, 0); // char**
            if (elem == "bool")
                return LLVMPointerType(bool_t_, 0);
            return LLVMPointerType(int32_t_, 0);
        }
        if (!type.empty() && type.back() == '*')
        {
            size_t stars = 0;
            for (size_t i = type.size(); i > 0 && type[i - 1] == '*'; --i)
            {
                stars++;
            }
            std::string base = type.substr(0, type.size() - stars);

            LLVMTypeRef baseTy = nullptr;
            // Normalize common aliases
            if (base == "str")
            {
                baseTy = int8ptr_t_;
            }
            else if (base == "void" || base == "char")
            {
                baseTy = LLVMInt8TypeInContext(ctx_);
            }
            else if (base == "int")
            {
                baseTy = int32_t_;
            }
            else if (base == "float")
            {
                baseTy = float_t_;
            }
            else if (base == "double")
            {
                baseTy = double_t_;
            }
            else if (base == "bool")
            {
                baseTy = bool_t_;
            }
            else if (g_struct_types_)
            {
                auto sit = g_struct_types_->find(base);
                if (sit != g_struct_types_->end())
                {
                    baseTy = sit->second;
                }
            }
            if (!baseTy)
            {
                baseTy = LLVMInt8TypeInContext(ctx_);
            }
            LLVMTypeRef ty = baseTy;
            for (size_t i = 0; i < stars; ++i)
            {
                ty = LLVMPointerType(ty, 0);
            }
            return ty;
        }
        if (type == "int")
            return int32_t_;
        else if (type == "float")
            return LLVMFloatTypeInContext(ctx_);
        else if (type == "double")
            return LLVMDoubleTypeInContext(ctx_);
        else if (type == "str" || type == "str*")
            return int8ptr_t_;
        else if (type == "bool")
            return bool_t_;
        else if (type == "void")
            return LLVMVoidTypeInContext(ctx_);
        else
        {
            if (g_struct_types_)
            {
                auto sit = g_struct_types_->find(type);
                if (sit != g_struct_types_->end())
                {
                    return sit->second;
                }
            }
            if (verbose_)
                printf("[codegen] unknown type '%s', defaulting to int\n", type.c_str());
            return int32_t_;
        }
    };

    auto mapToQuarkType = [this](const std::string &type) -> QuarkType
    {
        if (type.size() > 2 && type.substr(type.size() - 2) == "[]")
        {
            return QuarkType::Array;
        }
        if (!type.empty() && type.back() == '*')
        {
            return QuarkType::Pointer;
        }
        if (type == "int")
            return QuarkType::Int;
        else if (type == "float")
            return QuarkType::Float;
        else if (type == "double")
            return QuarkType::Double;
        else if (type == "str" || type == "str*" || type == "char*")
            return QuarkType::String;
        else if (type == "bool")
            return QuarkType::Boolean;
        else if (type == "void")
            return QuarkType::Void;
        else if (type == "void*")
            return QuarkType::Pointer;
        else if (type == "char")
            return QuarkType::Int;
        else if (type.find("*") != std::string::npos)
            return QuarkType::Pointer;
        else
        {
            if (verbose_)
                printf("[codegen] unknown type '%s' for QuarkType, defaulting to Int\n", type.c_str());
            return QuarkType::Int;
        }
    };

    // Get return type
    LLVMTypeRef returnType = mapType(externFunc->returnType);

    // Get parameter types
    std::vector<LLVMTypeRef> paramTypes;
    bool isVariadic = false;
    for (const auto &param : externFunc->params)
    {
        if (param.second == "...")
        {
            isVariadic = true;
            if (verbose_)
                printf("[codegen] extern function param: %s type %s (variadic)\n",
                       param.first.c_str(), param.second.c_str());
            continue;
        }

        LLVMTypeRef paramType = mapType(param.second); // param.second is the type
        paramTypes.push_back(paramType);
        if (verbose_)
            printf("[codegen] extern function param: %s type %s\n",
                   param.first.c_str(), param.second.c_str());
    }

    // Create function type
    LLVMTypeRef funcType = LLVMFunctionType(returnType,
                                            paramTypes.empty() ? nullptr : paramTypes.data(),
                                            static_cast<unsigned int>(paramTypes.size()),
                                            isVariadic ? 1 : 0); // set variadic flag

    // Declare the function
    LLVMValueRef func = LLVMAddFunction(module_, externFunc->name.c_str(), funcType);

    LLVMSetLinkage(func, LLVMExternalLinkage);

    // Store in function map
    (*g_function_map_)[externFunc->name] = func;
    (*g_function_param_types_)[externFunc->name] = paramTypes;
    (*g_variadic_functions_)[externFunc->name] = isVariadic;

    QuarkType returnQuarkType = mapToQuarkType(externFunc->returnType);
    expressionCodeGen_->declareFunctionType(externFunc->name, returnQuarkType, {});

    if (verbose_)
        printf("[codegen] declared extern function: %s\n", externFunc->name.c_str());
}

void StatementCodeGen::collectExternVariables(StmtAST *stmt, std::vector<ExternVarAST *> &externVars)
{
    if (verbose_)
        printf("[codegen] collectExternVariables called\n");

    if (auto *externVar = dynamic_cast<ExternVarAST *>(stmt))
    {
        if (verbose_)
            printf("[codegen] found extern variable: %s\n", externVar->name.c_str());
        externVars.push_back(externVar);
    }
    else if (auto *inc = dynamic_cast<IncludeStmt *>(stmt))
    {
        if (verbose_)
            printf("[codegen] found include statement with %zu statements\n", inc->stmts.size());
        for (auto &s : inc->stmts)
        {
            collectExternVariables(s.get(), externVars);
        }
    }
}

void StatementCodeGen::declareExternVariable(ExternVarAST *externVar)
{
    if (!g_named_values_ || !g_named_types_)
        throw std::runtime_error("global symbol tables not initialized");

    // Check if already declared
    if (g_named_values_->find(externVar->name) != g_named_values_->end())
    {
        if (verbose_)
            printf("[codegen] extern variable '%s' already declared, skipping\n", externVar->name.c_str());
        return;
    }

    if (verbose_)
        printf("[codegen] declaring extern variable: %s with type %s\n",
               externVar->name.c_str(), externVar->typeName.c_str());

    // Map the type string to an LLVM type
    auto mapType = [this](const std::string &type) -> LLVMTypeRef
    {
        if (!type.empty() && type.back() == '*')
        {
            size_t stars = 0;
            for (size_t i = type.size(); i > 0 && type[i - 1] == '*'; --i)
            {
                stars++;
            }
            std::string base = type.substr(0, type.size() - stars);

            LLVMTypeRef baseTy = nullptr;
            if (base == "str")
            {
                baseTy = int8ptr_t_;
            }
            else if (base == "void" || base == "char")
            {
                baseTy = LLVMInt8TypeInContext(ctx_);
            }
            else if (base == "int")
            {
                baseTy = int32_t_;
            }
            else if (base == "float")
            {
                baseTy = float_t_;
            }
            else if (base == "double")
            {
                baseTy = double_t_;
            }
            else if (base == "bool")
            {
                baseTy = bool_t_;
            }
            else if (g_struct_types_)
            {
                auto sit = g_struct_types_->find(base);
                if (sit != g_struct_types_->end())
                {
                    baseTy = sit->second;
                }
            }
            if (!baseTy)
            {
                baseTy = LLVMInt8TypeInContext(ctx_);
            }
            LLVMTypeRef ty = baseTy;
            for (size_t i = 0; i < stars; ++i)
            {
                ty = LLVMPointerType(ty, 0);
            }
            return ty;
        }
        if (type == "int")
            return int32_t_;
        else if (type == "float")
            return float_t_;
        else if (type == "double")
            return double_t_;
        else if (type == "str" || type == "char*")
            return int8ptr_t_;
        else if (type == "bool")
            return bool_t_;
        else if (type == "void")
            return LLVMVoidTypeInContext(ctx_);
        else
        {
            if (g_struct_types_)
            {
                auto sit = g_struct_types_->find(type);
                if (sit != g_struct_types_->end())
                {
                    return sit->second;
                }
            }
            if (verbose_)
                printf("[codegen] unknown type '%s' for extern var, defaulting to void*\n", type.c_str());
            return LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
        }
    };

    // Map type string to QuarkType
    auto mapToQuarkType = [](const std::string &type) -> QuarkType
    {
        if (type == "int")
            return QuarkType::Int;
        else if (type == "float")
            return QuarkType::Float;
        else if (type == "double")
            return QuarkType::Double;
        else if (type == "str" || type == "char*")
            return QuarkType::String;
        else if (type == "bool")
            return QuarkType::Boolean;
        else if (type == "void")
            return QuarkType::Void;
        else if (type.find('*') != std::string::npos)
            return QuarkType::Pointer;
        return QuarkType::Unknown;
    };

    LLVMTypeRef varType = mapType(externVar->typeName);
    QuarkType quarkType = mapToQuarkType(externVar->typeName);

    // Add external global variable
    LLVMValueRef globalVar = LLVMAddGlobal(module_, varType, externVar->name.c_str());
    LLVMSetLinkage(globalVar, LLVMExternalLinkage);
    LLVMSetExternallyInitialized(globalVar, 1); // Externally initialized

    // Store in the named values so it can be accessed
    (*g_named_values_)[externVar->name] = globalVar;
    (*g_named_types_)[externVar->name] = varType;

    // Register the type info for the expression codegen
    if (externVar->funcPtrInfo)
    {
        quarkType = QuarkType::FunctionPointer;
        expressionCodeGen_->declareVariable(externVar->name,
                                            TypeInfo(quarkType, {}, "", QuarkType::Unknown, 0, externVar->typeName, externVar->funcPtrInfo));
    }
    else
    {
        expressionCodeGen_->declareVariable(externVar->name, quarkType, {}, "", externVar->typeName);
    }

    if (verbose_)
        printf("[codegen] declared extern variable: %s with quark type %d\n", externVar->name.c_str(), (int)quarkType);
}

void StatementCodeGen::createStructType(StructDefStmt *structDef)
{
    if (!g_struct_defs_ || !g_struct_types_)
        throw std::runtime_error("struct symbol tables not initialized");

    if (verbose_)
        printf("[codegen] creating struct type: %s\n", structDef->name.c_str());

    // Store the struct definition
    (*g_struct_defs_)[structDef->name] = structDef;

    std::vector<LLVMTypeRef> fieldTypes;
    std::vector<std::pair<std::string, std::string>> allFields;

    std::function<void(const std::string &)> collectInheritedFields = [&](const std::string &structName)
    {
        auto it = g_struct_defs_->find(structName);
        if (it == g_struct_defs_->end())
        {
            throw std::runtime_error("Parent struct '" + structName + "' not found for struct " + structDef->name);
        }

        StructDefStmt *currentStruct = it->second;

        if (!currentStruct->parentName.empty())
        {
            collectInheritedFields(currentStruct->parentName);
        }

        // Add this struct's fields
        for (const auto &field : currentStruct->fields)
        {
            allFields.push_back(field);
        }
    };

    if (!structDef->parentName.empty())
    {
        if (verbose_)
            printf("[codegen] struct %s inherits from %s\n", structDef->name.c_str(), structDef->parentName.c_str());
        collectInheritedFields(structDef->parentName);
    }

    // Add this struct's own fields
    for (const auto &field : structDef->fields)
    {
        allFields.push_back(field);
    }

    for (const auto &field : allFields)
    {
        const std::string &fieldType = field.second;
        LLVMTypeRef llvmType;

        if (fieldType == "str")
        {
            llvmType = int8ptr_t_;
        }
        else if (fieldType == "int")
        {
            llvmType = int32_t_;
        }
        else if (fieldType == "float")
        {
            llvmType = LLVMFloatTypeInContext(ctx_);
        }
        else if (fieldType == "double")
        {
            llvmType = LLVMDoubleTypeInContext(ctx_);
        }
        else if (fieldType == "bool")
        {
            llvmType = bool_t_;
        }
        else if (fieldType.size() > 2 && fieldType.substr(fieldType.size() - 2) == "[]")
        {
            std::string elementType = fieldType.substr(0, fieldType.size() - 2);
            if (elementType == "str")
            {
                llvmType = LLVMPointerType(int8ptr_t_, 0);
            }
            else if (elementType == "int")
            {
                llvmType = LLVMPointerType(int32_t_, 0);
            }
            else if (elementType == "bool")
            {
                llvmType = LLVMPointerType(bool_t_, 0);
            }
            else
            {
                throw std::runtime_error("Unknown array element type '" + elementType + "' in struct " + structDef->name);
            }
        }
        else if (!fieldType.empty() && fieldType.back() == '*')
        {
            std::string base = fieldType.substr(0, fieldType.size() - 1);
            if (base == "void" || base == "char" || base == "str")
            {
                llvmType = LLVMPointerType(int8ptr_t_, 0);
            }
            else if (base == "int")
            {
                llvmType = LLVMPointerType(int32_t_, 0);
            }
            else if (base == "float")
            {
                llvmType = LLVMPointerType(LLVMFloatTypeInContext(ctx_), 0);
            }
            else if (base == "double")
            {
                llvmType = LLVMPointerType(LLVMDoubleTypeInContext(ctx_), 0);
            }
            else if (base == "bool")
            {
                llvmType = LLVMPointerType(bool_t_, 0);
            }
            else
            {
                auto sit = g_struct_types_->find(base);
                if (sit != g_struct_types_->end())
                {
                    llvmType = LLVMPointerType(sit->second, 0);
                }
                else
                {
                    llvmType = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
                }
            }
        }
        else
        {
            auto sit = g_struct_types_->find(fieldType);
            if (sit != g_struct_types_->end())
            {
                llvmType = sit->second;
            }
            else
            {
                throw std::runtime_error("Unknown field type '" + fieldType + "' in struct " + structDef->name);
            }
        }

        fieldTypes.push_back(llvmType);
    }

    // Create the LLVM struct type
    LLVMTypeRef structType = LLVMStructTypeInContext(ctx_, fieldTypes.data(), static_cast<unsigned int>(fieldTypes.size()), 0);
    (*g_struct_types_)[structDef->name] = structType;

    if (verbose_)
        printf("[codegen] created struct type: %s with %zu total fields (%zu inherited + %zu own)\n",
               structDef->name.c_str(), fieldTypes.size(),
               allFields.size() - structDef->fields.size(), structDef->fields.size());
}

void StatementCodeGen::genStructDefStmt(StructDefStmt *structDef)
{
    if (!g_struct_defs_ || !g_struct_types_)
        throw std::runtime_error("struct symbol tables not initialized");

    if (verbose_)
        printf("[codegen] processing struct definition: %s\n", structDef->name.c_str());

    if (!structDef->methods.empty())
    {
        if (verbose_)
            printf("[codegen] generating bodies for %zu methods in struct %s\n", structDef->methods.size(), structDef->name.c_str());

        for (auto &method : structDef->methods)
        {
            if (verbose_)
                printf("[codegen] generating body for struct method: %s\n", method->name.c_str());

            if (method->name.find("::") == std::string::npos)
            {
                std::string originalName = method->name;
                std::string mangledName = structDef->name + "::" + originalName;
                method->name = mangledName;
                genFunctionStmt(method.get(), nullptr);

                // Restore the original name
                method->name = originalName;
            }
            else
            {
                genFunctionStmt(method.get(), nullptr);
            }

            if (verbose_)
                printf("[codegen] completed body for struct method: %s\n", method->name.c_str());
        }
    }
}

void StatementCodeGen::genImplStmt(ImplStmt *impl, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] processing impl block for %s with %zu methods\n", impl->structName.c_str(), impl->methods.size());

    for (auto &method : impl->methods)
    {
        if (verbose_)
            printf("[codegen] generating method: %s\n", method->name.c_str());
        genFunctionStmt(method.get(), putsFn);
    }

    if (verbose_)
        printf("[codegen] completed impl block for %s\n", impl->structName.c_str());
}

void StatementCodeGen::genMemberAssignStmt(MemberAssignStmt *memberAssign)
{
    if (verbose_)
        printf("[codegen] processing member assignment\n");

    // Handle nested member access (e.g., this.author.id = value)
    if (auto *nestedAccess = dynamic_cast<MemberAccessExpr *>(memberAssign->object.get()))
    {
        // We need to get a pointer to the nested struct field, then GEP into it
        // First, get the type info of the nested object
        TypeInfo nestedTypeInfo = expressionCodeGen_->inferType(nestedAccess);
        if (nestedTypeInfo.type != QuarkType::Struct)
        {
            throw std::runtime_error("nested member assignment on non-struct type");
        }

        // Get the struct definition for the nested type
        auto defIt = g_struct_defs_->find(nestedTypeInfo.structName);
        if (defIt == g_struct_defs_->end())
        {
            throw std::runtime_error("struct definition not found: " + nestedTypeInfo.structName);
        }

        // Get the struct LLVM type
        auto structTypeIt = g_struct_types_->find(nestedTypeInfo.structName);
        if (structTypeIt == g_struct_types_->end())
        {
            throw std::runtime_error("struct LLVM type not found: " + nestedTypeInfo.structName);
        }
        LLVMTypeRef nestedStructType = structTypeIt->second;

        // Collect all fields including inherited ones
        std::vector<std::pair<std::string, std::string>> allFields;
        std::function<void(const std::string &)> collectInheritedFields = [&](const std::string &currentStructName)
        {
            auto currentIt = g_struct_defs_->find(currentStructName);
            if (currentIt == g_struct_defs_->end())
                return;
            StructDefStmt *currentStruct = currentIt->second;
            if (!currentStruct->parentName.empty())
            {
                collectInheritedFields(currentStruct->parentName);
            }
            for (const auto &field : currentStruct->fields)
            {
                allFields.push_back(field);
            }
        };
        collectInheritedFields(nestedTypeInfo.structName);

        // Find the field index for the target field
        int fieldIndex = -1;
        std::string fieldType;
        for (size_t i = 0; i < allFields.size(); ++i)
        {
            if (allFields[i].first == memberAssign->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                fieldType = allFields[i].second;
                break;
            }
        }
        if (fieldIndex == -1)
        {
            throw std::runtime_error("Field '" + memberAssign->fieldName + "' not found in struct " + nestedTypeInfo.structName);
        }

        // Now we need to get a pointer to the nested struct
        // We need to walk the chain and get GEP pointers all the way down
        LLVMValueRef nestedStructPtr = genNestedMemberPtr(nestedAccess);

        // Get pointer to the field within the nested struct
        LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, nestedStructType, nestedStructPtr, fieldIndex, "nested_field_ptr");

        // Generate the value to store
        LLVMValueRef value;
        if (fieldType == "str")
        {
            value = expressionCodeGen_->genExpr(memberAssign->value.get());
        }
        else if (fieldType == "int")
        {
            value = expressionCodeGen_->genExprInt(memberAssign->value.get());
        }
        else if (fieldType == "bool")
        {
            value = expressionCodeGen_->genExprBool(memberAssign->value.get());
        }
        else
        {
            value = expressionCodeGen_->genExpr(memberAssign->value.get());
        }

        // Store the value
        LLVMBuildStore(builder_, value, fieldPtr);

        if (verbose_)
            printf("[codegen] completed nested member assignment\n");
        return;
    }

    if (auto *varExpr = dynamic_cast<VariableExprAST *>(memberAssign->object.get()))
    {
        auto it = g_named_values_->find(varExpr->name);
        if (it == g_named_values_->end())
        {
            throw std::runtime_error("Unknown variable '" + varExpr->name + "' in member assignment");
        }

        LLVMValueRef objectPtrForGEP = nullptr; // must be struct*
        LLVMTypeRef structType = nullptr;       // underlying struct type

        if (varExpr->name == "this")
        {
            std::string structNameFromCtx;
            size_t pos = currentFunctionName_.find("::");
            if (pos != std::string::npos)
            {
                structNameFromCtx = currentFunctionName_.substr(0, pos);
            }
            if (!structNameFromCtx.empty())
            {
                auto stIt = g_struct_types_->find(structNameFromCtx);
                if (stIt != g_struct_types_->end())
                {
                    structType = stIt->second;
                    LLVMValueRef objectAlloca = it->second; // alloca of struct*
                    LLVMTypeRef ptrToStructTy = LLVMPointerType(structType, 0);
                    objectPtrForGEP = LLVMBuildLoad2(builder_, ptrToStructTy, objectAlloca, "this.ptr.load");
                }
            }
        }

        if (!structType || !objectPtrForGEP)
        {
            LLVMValueRef objectAlloca = it->second;
            LLVMTypeRef allocaTy = LLVMTypeOf(objectAlloca); // pointer to allocated type
            if (LLVMGetTypeKind(allocaTy) != LLVMPointerTypeKind)
            {
                throw std::runtime_error("expected pointer for variable '" + varExpr->name + "'");
            }
            LLVMTypeRef allocatedTy = LLVMGetElementType(allocaTy); // the allocated type
            if (LLVMGetTypeKind(allocatedTy) == LLVMStructTypeKind)
            {
                structType = allocatedTy;
                objectPtrForGEP = objectAlloca; // already struct*
            }
            else if (LLVMGetTypeKind(allocatedTy) == LLVMPointerTypeKind)
            {
                LLVMTypeRef elemTy = LLVMGetElementType(allocatedTy);
                if (elemTy && LLVMGetTypeKind(elemTy) == LLVMStructTypeKind)
                {
                    structType = elemTy;
                    objectPtrForGEP = LLVMBuildLoad2(builder_, allocatedTy, objectAlloca, "obj.ptr.load"); // yields struct*
                }
                else
                {
                    throw std::runtime_error("Variable '" + varExpr->name + "' is not a struct type");
                }
            }
            else
            {
                throw std::runtime_error("Variable '" + varExpr->name + "' is not a struct type");
            }
        }

        std::string structName;
        for (const auto &pair : *g_struct_types_)
        {
            if (pair.second == structType)
            {
                structName = pair.first;
                break;
            }
        }

        if (structName.empty())
        {
            throw std::runtime_error("Could not find struct name for type");
        }

        auto structDefIt = g_struct_defs_->find(structName);
        if (structDefIt == g_struct_defs_->end())
        {
            throw std::runtime_error("Struct definition not found for " + structName);
        }

        StructDefStmt *structDef = structDefIt->second;

        std::vector<std::pair<std::string, std::string>> allFields;
        std::function<void(const std::string &)> collectInheritedFields = [&](const std::string &currentStructName)
        {
            auto currentIt = g_struct_defs_->find(currentStructName);
            if (currentIt == g_struct_defs_->end())
            {
                return;
            }
            StructDefStmt *currentStruct = currentIt->second;

            if (!currentStruct->parentName.empty())
            {
                collectInheritedFields(currentStruct->parentName);
            }

            // Add this struct's fields
            for (const auto &field : currentStruct->fields)
            {
                allFields.push_back(field);
            }
        };

        collectInheritedFields(structName);

        int fieldIndex = -1;
        std::string fieldType;
        for (size_t i = 0; i < allFields.size(); ++i)
        {
            if (allFields[i].first == memberAssign->fieldName)
            {
                fieldIndex = static_cast<int>(i);
                fieldType = allFields[i].second;
                break;
            }
        }

        if (fieldIndex == -1)
        {
            throw std::runtime_error("Field '" + memberAssign->fieldName + "' not found in struct " + structName);
        }

        // Get pointer to the field
        LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structType, objectPtrForGEP, fieldIndex, "field_ptr");

        LLVMValueRef value;
        if (fieldType == "str")
        {
            value = expressionCodeGen_->genExpr(memberAssign->value.get());
        }
        else if (fieldType == "int")
        {
            value = expressionCodeGen_->genExprInt(memberAssign->value.get());
        }
        else if (fieldType == "bool")
        {
            value = expressionCodeGen_->genExprBool(memberAssign->value.get());
        }
        else
        {
            value = expressionCodeGen_->genExpr(memberAssign->value.get());
        }

        // Store the value
        LLVMBuildStore(builder_, value, fieldPtr);

        if (verbose_)
            printf("[codegen] completed member assignment: %s.%s\n", varExpr->name.c_str(), memberAssign->fieldName.c_str());
    }
    else
    {
        throw std::runtime_error("Member assignment on non-variable expressions not supported");
    }
}

LLVMValueRef StatementCodeGen::genNestedMemberPtr(MemberAccessExpr *memberAccess)
{
    // This function returns a pointer to the struct field for nested assignment
    // e.g., for this.author, it returns a pointer to the author field

    if (auto *varExpr = dynamic_cast<VariableExprAST *>(memberAccess->object.get()))
    {
        // Base case: variable.field - get pointer to the field
        auto it = g_named_values_->find(varExpr->name);
        if (it == g_named_values_->end())
        {
            throw std::runtime_error("Unknown variable '" + varExpr->name + "' in nested member access");
        }

        // Get the struct type info
        auto localIt = expressionCodeGen_->variableTypes_.find(varExpr->name);
        if (localIt == expressionCodeGen_->variableTypes_.end())
        {
            throw std::runtime_error("Type info not found for variable: " + varExpr->name);
        }
        const TypeInfo &typeInfo = localIt->second;
        if (typeInfo.type != QuarkType::Struct)
        {
            throw std::runtime_error("Variable is not a struct: " + varExpr->name);
        }

        // Get struct LLVM type
        auto structTypeIt = g_struct_types_->find(typeInfo.structName);
        if (structTypeIt == g_struct_types_->end())
        {
            throw std::runtime_error("Struct LLVM type not found: " + typeInfo.structName);
        }
        LLVMTypeRef structType = structTypeIt->second;

        // Collect all fields including inherited ones
        std::vector<std::pair<std::string, std::string>> allFields;
        std::function<void(const std::string &)> collectInheritedFields = [&](const std::string &currentStructName)
        {
            auto currentIt = g_struct_defs_->find(currentStructName);
            if (currentIt == g_struct_defs_->end())
                return;
            StructDefStmt *currentStruct = currentIt->second;
            if (!currentStruct->parentName.empty())
            {
                collectInheritedFields(currentStruct->parentName);
            }
            for (const auto &field : currentStruct->fields)
            {
                allFields.push_back(field);
            }
        };
        collectInheritedFields(typeInfo.structName);

        // Find the field index
        int fieldIndex = -1;
        for (size_t i = 0; i < allFields.size(); i++)
        {
            if (allFields[i].first == memberAccess->fieldName)
            {
                fieldIndex = (int)i;
                break;
            }
        }
        if (fieldIndex == -1)
        {
            throw std::runtime_error("Field not found: " + memberAccess->fieldName + " in struct " + typeInfo.structName);
        }

        // Get the base pointer
        LLVMValueRef objectAlloca = it->second;
        LLVMValueRef objectPtrForGEP = nullptr;

        if (varExpr->name == "this")
        {
            // For 'this', load the pointer from the alloca
            LLVMTypeRef ptrToStructTy = LLVMPointerType(structType, 0);
            objectPtrForGEP = LLVMBuildLoad2(builder_, ptrToStructTy, objectAlloca, "this.ptr.load");
        }
        else
        {
            // For regular variables, check if it's an alloca of struct or pointer to struct
            LLVMTypeRef allocaTy = LLVMTypeOf(objectAlloca);
            LLVMTypeRef allocatedTy = LLVMGetElementType(allocaTy);
            if (LLVMGetTypeKind(allocatedTy) == LLVMStructTypeKind)
            {
                objectPtrForGEP = objectAlloca;
            }
            else if (LLVMGetTypeKind(allocatedTy) == LLVMPointerTypeKind)
            {
                objectPtrForGEP = LLVMBuildLoad2(builder_, allocatedTy, objectAlloca, "obj.ptr.load");
            }
            else
            {
                throw std::runtime_error("Variable '" + varExpr->name + "' is not a struct type");
            }
        }

        // Build GEP to get pointer to the field
        LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structType, objectPtrForGEP, fieldIndex, "nested_member_ptr");
        return fieldPtr;
    }
    else if (auto *nestedAccess = dynamic_cast<MemberAccessExpr *>(memberAccess->object.get()))
    {
        // Recursive case: nested.field.field - get pointer to nested field first, then GEP further
        LLVMValueRef outerFieldPtr = genNestedMemberPtr(nestedAccess);

        // Get type info for the outer field
        TypeInfo nestedTypeInfo = expressionCodeGen_->inferType(nestedAccess);
        if (nestedTypeInfo.type != QuarkType::Struct)
        {
            throw std::runtime_error("Nested member access on non-struct type");
        }

        // Get struct LLVM type
        auto structTypeIt = g_struct_types_->find(nestedTypeInfo.structName);
        if (structTypeIt == g_struct_types_->end())
        {
            throw std::runtime_error("Struct LLVM type not found: " + nestedTypeInfo.structName);
        }
        LLVMTypeRef nestedStructType = structTypeIt->second;

        // Collect all fields including inherited ones
        std::vector<std::pair<std::string, std::string>> allFields;
        std::function<void(const std::string &)> collectInheritedFields = [&](const std::string &currentStructName)
        {
            auto currentIt = g_struct_defs_->find(currentStructName);
            if (currentIt == g_struct_defs_->end())
                return;
            StructDefStmt *currentStruct = currentIt->second;
            if (!currentStruct->parentName.empty())
            {
                collectInheritedFields(currentStruct->parentName);
            }
            for (const auto &field : currentStruct->fields)
            {
                allFields.push_back(field);
            }
        };
        collectInheritedFields(nestedTypeInfo.structName);

        // Find the field index
        int fieldIndex = -1;
        for (size_t i = 0; i < allFields.size(); i++)
        {
            if (allFields[i].first == memberAccess->fieldName)
            {
                fieldIndex = (int)i;
                break;
            }
        }
        if (fieldIndex == -1)
        {
            throw std::runtime_error("Field not found: " + memberAccess->fieldName + " in struct " + nestedTypeInfo.structName);
        }

        // Build GEP from the outer field pointer to get pointer to this field
        LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, nestedStructType, outerFieldPtr, fieldIndex, "deep_nested_member_ptr");
        return fieldPtr;
    }

    throw std::runtime_error("Unsupported nested member access pattern");
}

void StatementCodeGen::collectStructDefs(StmtAST *stmt, std::vector<StructDefStmt *> &structDefs)
{
    if (verbose_)
        printf("[codegen] collectStructDefs called\n");

    if (auto *structDef = dynamic_cast<StructDefStmt *>(stmt))
    {
        if (verbose_)
            printf("[codegen] found struct definition: %s\n", structDef->name.c_str());
        structDefs.push_back(structDef);
    }
    else if (auto *includeStmt = dynamic_cast<IncludeStmt *>(stmt))
    {
        if (verbose_)
            printf("[codegen] collectStructDefs: descending into include with %zu stmts\n", includeStmt->stmts.size());
        for (auto &s : includeStmt->stmts)
        {
            collectStructDefs(s.get(), structDefs);
        }
    }
    else
    {
        if (verbose_)
            printf("[codegen] statement is not a struct definition or include\n");
    }
}

void StatementCodeGen::processStructDef(StructDefStmt *structDef)
{
    createStructType(structDef);
}

void StatementCodeGen::genReturnStmt(ReturnStmt *retStmt)
{
    if (verbose_)
        printf("[codegen] processing return statement\n");

    if (retStmt->returnValue)
    {
        // Return with a value
        if (verbose_)
            printf("[codegen] returning expression (not a variable)\n");

        if (!currentFunctionReturnType_.empty())
        {
            auto structIt = g_struct_types_->find(currentFunctionReturnType_);
            if (structIt != g_struct_types_->end())
            {
                if (verbose_)
                    printf("[codegen] returning struct type: %s\n", currentFunctionReturnType_.c_str());
                LLVMValueRef structPtr = expressionCodeGen_->genExpr(retStmt->returnValue.get());
                LLVMValueRef structValue = LLVMBuildLoad2(builder_, structIt->second, structPtr, "ret_struct_load");
                LLVMBuildRet(builder_, structValue);
                return;
            }
        }

        LLVMValueRef returnValue;
        if (!currentFunctionReturnType_.empty() && currentFunctionReturnType_ == "int")
        {
            if (verbose_)
                printf("[codegen] using genExprInt for return\n");
            returnValue = expressionCodeGen_->genExprInt(retStmt->returnValue.get());
        }
        else if (!currentFunctionReturnType_.empty() && currentFunctionReturnType_ == "bool")
        {
            if (verbose_)
                printf("[codegen] using genExprBool for return\n");
            returnValue = expressionCodeGen_->genExprBool(retStmt->returnValue.get());
        }
        else
        {
            if (verbose_)
                printf("[codegen] using generic genExpr for return\n");
            returnValue = expressionCodeGen_->genExpr(retStmt->returnValue.get());
        }

        LLVMBuildRet(builder_, returnValue);
    }
    else
    {
        if (verbose_)
            printf("[codegen] returning void\n");
        LLVMBuildRetVoid(builder_);
    }

    if (verbose_)
        printf("[codegen] completed return statement\n");
}

void StatementCodeGen::genBreakStmt()
{
    if (loopStack_.empty())
    {
        throw std::runtime_error("'break' used outside of loop");
    }
    LLVMBasicBlockRef target = loopStack_.back().breakTarget;
    LLVMBuildBr(builder_, target);
    LLVMValueRef currentFn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(ctx_, currentFn, "after_break");
    LLVMPositionBuilderAtEnd(builder_, cont);
}

void StatementCodeGen::genContinueStmt()
{
    if (loopStack_.empty())
    {
        throw std::runtime_error("'continue' used outside of loop");
    }
    LLVMBasicBlockRef target = loopStack_.back().continueTarget;
    LLVMBuildBr(builder_, target);
    LLVMValueRef currentFn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(ctx_, currentFn, "after_continue");
    LLVMPositionBuilderAtEnd(builder_, cont);
}

void StatementCodeGen::genWhileStmt(WhileStmt *whileStmt, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] processing while statement\n");

    // Get current function context
    LLVMValueRef currentFn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
    if (!currentFn)
    {
        throw std::runtime_error("while statement generated outside function context");
    }

    if (auto *boolCond = dynamic_cast<BoolExprAST *>(whileStmt->condition.get()))
    {
        if (boolCond->value)
        {
            LLVMBasicBlockRef bodyBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_body");
            LLVMBasicBlockRef exitBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_exit");
            // Jump directly to body
            LLVMBuildBr(builder_, bodyBB);
            // Body
            LLVMPositionBuilderAtEnd(builder_, bodyBB);
            loopStack_.push_back({bodyBB, exitBB});
            for (size_t i = 0; i < whileStmt->body.size(); ++i)
            {
                genStmt(whileStmt->body[i].get(), putsFn);
            }
            loopStack_.pop_back();
            LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);
            if (currentBB && !LLVMGetBasicBlockTerminator(currentBB))
            {
                LLVMBuildBr(builder_, bodyBB);
            }
            LLVMPositionBuilderAtEnd(builder_, exitBB);
        }
        else
        {
            LLVMBasicBlockRef exitBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_exit");
            LLVMBuildBr(builder_, exitBB);
            LLVMPositionBuilderAtEnd(builder_, exitBB);
        }
        if (verbose_)
            printf("[codegen] completed while statement (constant condition)\n");
        return;
    }
    if (auto cval = expressionCodeGen_->evalConst(whileStmt->condition.get()))
    {
        bool truthy = (*cval != 0.0);
        if (truthy)
        {
            LLVMBasicBlockRef bodyBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_body");
            LLVMBasicBlockRef exitBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_exit");
            LLVMBuildBr(builder_, bodyBB);
            LLVMPositionBuilderAtEnd(builder_, bodyBB);
            loopStack_.push_back({bodyBB, exitBB});
            for (size_t i = 0; i < whileStmt->body.size(); ++i)
            {
                genStmt(whileStmt->body[i].get(), putsFn);
            }
            loopStack_.pop_back();
            LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);
            if (currentBB && !LLVMGetBasicBlockTerminator(currentBB))
            {
                LLVMBuildBr(builder_, bodyBB);
            }
            LLVMPositionBuilderAtEnd(builder_, exitBB);
        }
        else
        {
            LLVMBasicBlockRef exitBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_exit");
            LLVMBuildBr(builder_, exitBB);
            LLVMPositionBuilderAtEnd(builder_, exitBB);
        }
        if (verbose_)
            printf("[codegen] completed while statement (folded constant condition)\n");
        return;
    }

    LLVMBasicBlockRef condBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_cond");
    LLVMBasicBlockRef bodyBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_body");
    LLVMBasicBlockRef exitBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "while_exit");

    // Jump to condition block
    LLVMBuildBr(builder_, condBB);

    // Generate condition block
    LLVMPositionBuilderAtEnd(builder_, condBB);
    LLVMValueRef condVal = expressionCodeGen_->genExprBool(whileStmt->condition.get());
    LLVMBuildCondBr(builder_, condVal, bodyBB, exitBB);

    // Generate loop body
    LLVMPositionBuilderAtEnd(builder_, bodyBB);
    loopStack_.push_back({condBB, exitBB});
    for (size_t i = 0; i < whileStmt->body.size(); ++i)
    {
        genStmt(whileStmt->body[i].get(), putsFn);
    }
    loopStack_.pop_back();

    {
        LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);
        if (currentBB == bodyBB && !LLVMGetBasicBlockTerminator(bodyBB))
        {
            LLVMBuildBr(builder_, condBB);
        }
        else if (currentBB != bodyBB && currentBB && !LLVMGetBasicBlockTerminator(currentBB))
        {
            LLVMBuildBr(builder_, condBB);
        }
    }

    // Continue with exit block
    LLVMPositionBuilderAtEnd(builder_, exitBB);

    if (verbose_)
        printf("[codegen] completed while statement\n");
}

void StatementCodeGen::genForStmt(ForStmt *forStmt, LLVMValueRef putsFn)
{
    if (verbose_)
        printf("[codegen] processing for statement (basic range-based for loop)\n");

    // Get current function context
    LLVMValueRef currentFn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
    if (!currentFn)
    {
        throw std::runtime_error("for statement generated outside function context");
    }

    LLVMValueRef startVal = nullptr;
    LLVMValueRef endVal = nullptr;
    if (auto *range = dynamic_cast<RangeExpr *>(forStmt->rangeExpr.get()))
    {
        startVal = expressionCodeGen_->genExprInt(range->start.get());
        endVal = expressionCodeGen_->genExprInt(range->end.get());
    }
    else
    {
        startVal = LLVMConstInt(int32_t_, 0, 0);
        endVal = expressionCodeGen_->genExprInt(forStmt->rangeExpr.get());
    }

    // Create loop variable in entry block to avoid stack growth on each iteration
    LLVMValueRef loopVar = createEntryBlockAlloca(currentFn, int32_t_, forStmt->var.c_str());
    LLVMBuildStore(builder_, startVal, loopVar);

    (*g_named_values_)[forStmt->var] = loopVar;
    (*g_named_types_)[forStmt->var] = int32_t_;

    // Create basic blocks
    LLVMBasicBlockRef condBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "for_cond");
    LLVMBasicBlockRef bodyBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "for_body");
    LLVMBasicBlockRef incBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "for_inc");
    LLVMBasicBlockRef exitBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "for_exit");

    // Jump to condition
    LLVMBuildBr(builder_, condBB);

    // Condition: i < range
    LLVMPositionBuilderAtEnd(builder_, condBB);
    LLVMValueRef currentVal = LLVMBuildLoad2(builder_, int32_t_, loopVar, "loop_val");
    LLVMValueRef cond = LLVMBuildICmp(builder_, LLVMIntSLT, currentVal, endVal, "for_cond");
    LLVMBuildCondBr(builder_, cond, bodyBB, exitBB);

    // Body
    LLVMPositionBuilderAtEnd(builder_, bodyBB);
    loopStack_.push_back({incBB, exitBB});
    for (size_t i = 0; i < forStmt->body.size(); ++i)
    {
        genStmt(forStmt->body[i].get(), putsFn);
    }
    loopStack_.pop_back();

    LLVMBasicBlockRef currentBB = LLVMGetInsertBlock(builder_);

    if (currentBB == bodyBB && !LLVMGetBasicBlockTerminator(bodyBB))
    {
        LLVMBuildBr(builder_, incBB);
    }
    else if (currentBB != bodyBB && !LLVMGetBasicBlockTerminator(currentBB))
    {
        LLVMBuildBr(builder_, incBB);
    }

    // Increment: i++
    LLVMPositionBuilderAtEnd(builder_, incBB);
    LLVMValueRef incrementedVal = LLVMBuildAdd(builder_,
                                               LLVMBuildLoad2(builder_, int32_t_, loopVar, "loop_val"),
                                               LLVMConstInt(int32_t_, 1, 0), "inc_val");
    LLVMBuildStore(builder_, incrementedVal, loopVar);
    LLVMBuildBr(builder_, condBB);

    LLVMPositionBuilderAtEnd(builder_, exitBB);

    if (verbose_)
        printf("[codegen] completed for statement\n");
}

void StatementCodeGen::genDerefAssignStmt(DerefAssignStmt *derefAssign)
{
    if (verbose_)
        printf("[codegen] processing dereference assignment: *ptr = value\n");

    if (!derefAssign->value)
    {
        if (verbose_)
            printf("[codegen] dereference assignment has null value\n");
        return;
    }

    LLVMValueRef val = expressionCodeGen_->genExpr(derefAssign->value.get());

    LLVMValueRef ptrValue = expressionCodeGen_->genExpr(derefAssign->deref->operand.get());

    LLVMBuildStore(builder_, val, ptrValue);

    if (verbose_)
        printf("[codegen] completed dereference assignment\n");
}

void StatementCodeGen::genMatchStmt(MatchStmt *matchStmt)
{
    if (verbose_)
        printf("[codegen] generating match statement\n");

    if (!matchStmt->expr)
    {
        throw std::runtime_error("match statement requires an expression to match");
    }

    // Get the value to match
    LLVMValueRef matchValue = expressionCodeGen_->genExpr(matchStmt->expr.get());
    TypeInfo matchType = expressionCodeGen_->inferType(matchStmt->expr.get());

    // Get current function for creating basic blocks
    LLVMValueRef currentFn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
    if (!currentFn)
    {
        throw std::runtime_error("match statement generated outside function context");
    }

    // Create merge block for after the match
    LLVMBasicBlockRef mergeBB = LLVMAppendBasicBlockInContext(ctx_, currentFn, "match_merge");

    // Find the wildcard arm (if any) - it should be handled last
    int wildcardIndex = -1;
    for (size_t i = 0; i < matchStmt->arms.size(); ++i)
    {
        if (matchStmt->arms[i].isWildcard)
        {
            wildcardIndex = static_cast<int>(i);
            break;
        }
    }

    // Generate code for each arm
    std::vector<LLVMBasicBlockRef> armBlocks;
    std::vector<LLVMBasicBlockRef> nextCheckBlocks;

    for (size_t i = 0; i < matchStmt->arms.size(); ++i)
    {
        armBlocks.push_back(LLVMAppendBasicBlockInContext(ctx_, currentFn, ("match_arm_" + std::to_string(i)).c_str()));
        if (i < matchStmt->arms.size() - 1)
        {
            nextCheckBlocks.push_back(LLVMAppendBasicBlockInContext(ctx_, currentFn, ("match_check_" + std::to_string(i + 1)).c_str()));
        }
    }

    // Generate condition checks and branches
    for (size_t i = 0; i < matchStmt->arms.size(); ++i)
    {
        const MatchArm &arm = matchStmt->arms[i];

        if (arm.isWildcard)
        {
            // Wildcard matches everything - just branch to the arm
            LLVMBuildBr(builder_, armBlocks[i]);
        }
        else
        {
            // Generate comparison
            LLVMValueRef patternValue = expressionCodeGen_->genExpr(arm.pattern.get());
            LLVMValueRef cond = nullptr;

            // Generate appropriate comparison based on type
            if (matchType.type == QuarkType::Int || matchType.type == QuarkType::Boolean)
            {
                cond = LLVMBuildICmp(builder_, LLVMIntEQ, matchValue, patternValue, "match_cmp");
            }
            else if (matchType.type == QuarkType::Float || matchType.type == QuarkType::Double)
            {
                cond = LLVMBuildFCmp(builder_, LLVMRealOEQ, matchValue, patternValue, "match_cmp");
            }
            else if (matchType.type == QuarkType::String)
            {
                // For strings, call strcmp
                LLVMValueRef strcmpFn = LLVMGetNamedFunction(module_, "strcmp");
                if (!strcmpFn)
                {
                    LLVMTypeRef args[] = {int8ptr_t_, int8ptr_t_};
                    LLVMTypeRef fnType = LLVMFunctionType(int32_t_, args, 2, 0);
                    strcmpFn = LLVMAddFunction(module_, "strcmp", fnType);
                }
                LLVMValueRef strcmpArgs[] = {matchValue, patternValue};
                LLVMValueRef result = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strcmpFn), strcmpFn, strcmpArgs, 2, "strcmp_result");
                cond = LLVMBuildICmp(builder_, LLVMIntEQ, result, LLVMConstInt(int32_t_, 0, 0), "match_cmp");
            }
            else if (matchType.type == QuarkType::Pointer || matchType.type == QuarkType::Null)
            {
                // Pointer comparison
                cond = LLVMBuildICmp(builder_, LLVMIntEQ,
                                     LLVMBuildPtrToInt(builder_, matchValue, LLVMInt64TypeInContext(ctx_), "ptr_to_int1"),
                                     LLVMBuildPtrToInt(builder_, patternValue, LLVMInt64TypeInContext(ctx_), "ptr_to_int2"),
                                     "match_cmp");
            }
            else
            {
                // Default to integer comparison
                cond = LLVMBuildICmp(builder_, LLVMIntEQ, matchValue, patternValue, "match_cmp");
            }

            // Branch based on comparison
            LLVMBasicBlockRef nextBlock = (i < matchStmt->arms.size() - 1) ? nextCheckBlocks[i] : mergeBB;
            LLVMBuildCondBr(builder_, cond, armBlocks[i], nextBlock);
        }

        // Generate arm body
        LLVMPositionBuilderAtEnd(builder_, armBlocks[i]);
        for (auto &stmt : arm.body)
        {
            genStmt(stmt.get(), nullptr);
        }
        // Branch to merge if not terminated
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder_)))
        {
            LLVMBuildBr(builder_, mergeBB);
        }

        // Position at next check block for next iteration (if not last and not wildcard)
        if (i < matchStmt->arms.size() - 1 && !arm.isWildcard)
        {
            LLVMPositionBuilderAtEnd(builder_, nextCheckBlocks[i]);
        }
    }

    // Position at merge block for subsequent code
    LLVMPositionBuilderAtEnd(builder_, mergeBB);

    if (verbose_)
        printf("[codegen] completed match statement\n");
}

void StatementCodeGen::genArrayAssignStmt(ArrayAssignStmt *arrayAssign)
{
    if (verbose_)
        printf("[codegen] generating array assignment\n");

    TypeInfo arrayTypeInfo = expressionCodeGen_->inferType(arrayAssign->array.get());

    // Handle list subscript assignment: list[index] = value
    if (arrayTypeInfo.type == QuarkType::List)
    {
        if (verbose_)
            printf("[codegen] generating list subscript assignment\n");

        LLVMValueRef listPtr = expressionCodeGen_->genExpr(arrayAssign->array.get());
        LLVMValueRef indexValue = expressionCodeGen_->genExprInt(arrayAssign->index.get());
        LLVMValueRef value = expressionCodeGen_->genExpr(arrayAssign->value.get());

        // Convert value to i8*
        LLVMValueRef valueAsI8Ptr;
        LLVMTypeRef valType = LLVMTypeOf(value);
        LLVMTypeKind kind = LLVMGetTypeKind(valType);
        if (kind == LLVMPointerTypeKind)
        {
            valueAsI8Ptr = LLVMBuildPointerCast(builder_, value, int8ptr_t_, "val_i8p");
        }
        else if (kind == LLVMIntegerTypeKind)
        {
            valueAsI8Ptr = LLVMBuildIntToPtr(builder_, value, int8ptr_t_, "int_to_ptr");
        }
        else if (kind == LLVMFloatTypeKind || kind == LLVMDoubleTypeKind)
        {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx_);
            LLVMValueRef bits;
            if (kind == LLVMFloatTypeKind)
            {
                bits = LLVMBuildBitCast(builder_, value, LLVMInt32TypeInContext(ctx_), "float_bits");
                bits = LLVMBuildZExt(builder_, bits, i64, "float_bits_ext");
            }
            else
            {
                bits = LLVMBuildBitCast(builder_, value, i64, "double_bits");
            }
            valueAsI8Ptr = LLVMBuildIntToPtr(builder_, bits, int8ptr_t_, "bits_to_ptr");
        }
        else
        {
            valueAsI8Ptr = LLVMBuildPointerCast(builder_, value, int8ptr_t_, "val_i8p");
        }

        // Get __quark_list_set function
        LLVMValueRef listSetFn = LLVMGetNamedFunction(module_, "__quark_list_set");
        if (!listSetFn)
        {
            expressionCodeGen_->declareNativeListRuntime();
            listSetFn = LLVMGetNamedFunction(module_, "__quark_list_set");
        }

        LLVMValueRef args[] = {listPtr, indexValue, valueAsI8Ptr};
        LLVMBuildCall2(builder_, LLVMGlobalGetValueType(listSetFn), listSetFn, args, 3, "");

        if (verbose_)
            printf("[codegen] completed list subscript assignment\n");
        return;
    }

    LLVMValueRef arrayPtr = expressionCodeGen_->genExpr(arrayAssign->array.get());

    LLVMValueRef indexValue = expressionCodeGen_->genExprInt(arrayAssign->index.get());

    // Generate the value to assign
    LLVMValueRef value = expressionCodeGen_->genExpr(arrayAssign->value.get());

    LLVMTypeRef elementType = int32_t_; // default

    if (arrayTypeInfo.type == QuarkType::Array)
    {
        elementType = expressionCodeGen_->quarkTypeToLLVMType(arrayTypeInfo.elementType);

        if (arrayTypeInfo.elementType == QuarkType::Struct && g_struct_types_)
        {
            elementType = int8ptr_t_;
        }
    }

    LLVMTypeRef desiredPtrTy = LLVMPointerType(elementType, 0);
    if (LLVMTypeOf(arrayPtr) != desiredPtrTy)
    {
        arrayPtr = LLVMBuildPointerCast(builder_, arrayPtr, desiredPtrTy, "arr_as_elem_ptr");
    }
    LLVMValueRef elementPtr = LLVMBuildGEP2(builder_, elementType, arrayPtr, &indexValue, 1, "array_assign_ptr");

    LLVMBuildStore(builder_, value, elementPtr);

    if (verbose_)
        printf("[codegen] completed array assignment\n");
}

LLVMValueRef StatementCodeGen::createEntryBlockAlloca(LLVMValueRef function, LLVMTypeRef type, const char *name)
{
    // Get the entry block of the function
    LLVMBasicBlockRef entryBlock = LLVMGetEntryBasicBlock(function);

    // Create a new builder for inserting the alloca
    LLVMBuilderRef allocaBuilder = LLVMCreateBuilderInContext(ctx_);

    // Position at the beginning of the entry block
    LLVMValueRef firstInstr = LLVMGetFirstInstruction(entryBlock);
    if (firstInstr)
    {
        LLVMPositionBuilderBefore(allocaBuilder, firstInstr);
    }
    else
    {
        LLVMPositionBuilderAtEnd(allocaBuilder, entryBlock);
    }

    // Create the alloca instruction
    LLVMValueRef alloca = LLVMBuildAlloca(allocaBuilder, type, name);

    // Clean up the temporary builder
    LLVMDisposeBuilder(allocaBuilder);

    return alloca;
}
