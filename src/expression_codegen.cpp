#include "../include/expression_codegen.h"
#include "../include/compilation_context.h"
#include "../include/error_reporter.h"
#include "../include/source_manager.h"
#include <stdexcept>
#include <cstdio>
#include <optional>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <functional>

ExpressionCodeGen::ExpressionCodeGen(LLVMContextRef ctx, LLVMModuleRef module, LLVMBuilderRef builder, 
                                     ExternalFunctions* externalFunctions, bool verbose,
                                     CompilationContext* compilationCtx)
    : ctx_(ctx), module_(module), builder_(builder), externalFunctions_(externalFunctions), 
      builtinFunctions_(nullptr), verbose_(verbose), ctx_compilation_(compilationCtx)
{
    int32_t_ = LLVMInt32TypeInContext(ctx_);
    int8ptr_t_ = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);

#ifdef _WIN32
    fileptr_t_ = LLVMPointerType(LLVMStructCreateNamed(ctx_, "struct._iobuf"), 0);
#else
    fileptr_t_ = LLVMPointerType(LLVMStructCreateNamed(ctx_, "struct._IO_FILE"), 0);
#endif
    bool_t_ = LLVMInt1TypeInContext(ctx_);
    float_t_ = LLVMFloatTypeInContext(ctx_);
    double_t_ = LLVMDoubleTypeInContext(ctx_);
    
    g_function_map_ = nullptr;
    g_named_values_ = nullptr;
    g_const_values_ = nullptr;
    g_function_param_types_ = nullptr;
    g_named_types_ = nullptr;
    g_struct_defs_ = nullptr;
    g_struct_types_ = nullptr;
    g_variadic_functions_ = nullptr;
}

void ExpressionCodeGen::setBuiltinFunctions(BuiltinFunctions* builtinFunctions) {
    builtinFunctions_ = builtinFunctions;
}

ErrorReporter* ExpressionCodeGen::errorReporter() const {
    if (ctx_compilation_) return &ctx_compilation_->errorReporter;
    return g_errorReporter.get();
}

SourceManager* ExpressionCodeGen::sourceManager() const {
    if (ctx_compilation_) return &ctx_compilation_->sourceManager;
    return g_sourceManager.get();
}

void ExpressionCodeGen::setGlobalSymbolTables(std::unordered_map<std::string, LLVMValueRef>* functionMap,
                                              std::unordered_map<std::string, LLVMValueRef>* namedValues,
                                              std::unordered_map<std::string, double>* constValues,
                                              std::unordered_map<std::string, std::vector<LLVMTypeRef>>* functionParamTypes,
                                              std::unordered_map<std::string, LLVMTypeRef>* namedTypes,
                                              std::unordered_map<std::string, StructDefStmt*>* structDefs,
                                              std::unordered_map<std::string, LLVMTypeRef>* structTypes,
                                              std::unordered_map<std::string, bool>* variadicFunctions)
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

void ExpressionCodeGen::setFunctionParameters(const std::unordered_map<std::string, bool>& functionParams)
{
    function_params_ = functionParams;
}

void ExpressionCodeGen::clearFunctionParameters()
{
    function_params_.clear();
}

void ExpressionCodeGen::registerStructField(const std::string& fieldName, const std::string& structName, const std::string& fieldType)
{
    struct_fields_[fieldName] = std::make_pair(structName, fieldType);
    if (verbose_) {
        printf("[expression_codegen] registered struct field: %s (type: %s) for struct %s\n", 
               fieldName.c_str(), fieldType.c_str(), structName.c_str());
    }
}

void ExpressionCodeGen::clearStructFields()
{
    struct_fields_.clear();
}

// Type checking implementation
TypeInfo ExpressionCodeGen::inferType(ExprAST *expr)
{
    if (!expr) return TypeInfo(QuarkType::Unknown, {});
    
    // Handle struct literals
    if (auto *structLiteral = dynamic_cast<StructLiteralExpr *>(expr)) {
        return TypeInfo(QuarkType::Struct, expr->location, structLiteral->structName);
    }
    
    // Handle member access
    if (auto *memberAccess = dynamic_cast<MemberAccessExpr *>(expr)) {
                if (auto *varExpr = dynamic_cast<VariableExprAST *>(memberAccess->object.get())) {
                        std::string objectStructName;
            auto it = variableTypes_.find(varExpr->name);
            if (it != variableTypes_.end()) {
                if (it->second.type != QuarkType::Struct) {
                    throw CodeGenError("variable '" + varExpr->name + "' is not a struct", expr->location);
                }
                objectStructName = it->second.structName;
            } else if (g_named_types_) {
                auto git = g_named_types_->find(varExpr->name);
                if (git != g_named_types_->end() && g_struct_types_) {
                    // Map LLVM type to struct name
                    for (const auto &p : *g_struct_types_) {
                        if (p.second == git->second) { objectStructName = p.first; break; }
                    }
                }
            }
            if (objectStructName.empty()) {
                                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                    auto file = sm->getFile(expr->location.filename);
                    if (file) {
                        throw EnhancedCodeGenError("undefined variable '" + varExpr->name + "'", expr->location, file->content, ErrorCodes::SYMBOL_NOT_FOUND, (int)std::max<size_t>(1, varExpr->name.size()));
                    }
                }
                throw CodeGenError("undefined variable '" + varExpr->name + "'", expr->location);
            }
            
            // Find the struct definition
            auto defIt = g_struct_defs_->find(objectStructName);
            if (defIt == g_struct_defs_->end()) {
                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                    auto file = sm->getFile(expr->location.filename);
                    if (file) {
                        throw EnhancedCodeGenError("struct definition not found: " + objectStructName, expr->location, file->content, ErrorCodes::SYMBOL_NOT_FOUND, (int)std::max<size_t>(1, objectStructName.size()));
                    }
                }
                throw CodeGenError("struct definition not found: " + objectStructName, expr->location);
            }
            
                        const StructDefStmt* structDef = defIt->second;
            
                        std::vector<std::pair<std::string, std::string>> allFields;
            std::function<void(const std::string&)> collectInheritedFields = [&](const std::string& currentStructName) {
                auto currentIt = g_struct_defs_->find(currentStructName);
                if (currentIt == g_struct_defs_->end()) {
                    return;
                }
                const StructDefStmt* currentStruct = currentIt->second;
                
                                if (!currentStruct->parentName.empty()) {
                    collectInheritedFields(currentStruct->parentName);
                }
                
                // Add this struct's fields
                for (const auto& field : currentStruct->fields) {
                    allFields.push_back(field);
                }
            };
            
            collectInheritedFields(objectStructName);
            
            for (const auto& field : allFields) {
                if (field.first == memberAccess->fieldName) {
                                        QuarkType fieldType = QuarkType::Unknown;
                    if (field.second == "int") fieldType = QuarkType::Int;
                    else if (field.second == "float") fieldType = QuarkType::Float;
                    else if (field.second == "double") fieldType = QuarkType::Double;
                    else if (field.second == "str") fieldType = QuarkType::String;
                    else if (field.second == "bool") fieldType = QuarkType::Boolean;
                    else if (field.second.size() > 2 && field.second.substr(field.second.size() - 2) == "[]") {
                                                std::string elementTypeName = field.second.substr(0, field.second.size() - 2);
                        QuarkType elementType = QuarkType::Unknown;
                        if (elementTypeName == "str") elementType = QuarkType::String;
                        else if (elementTypeName == "int") elementType = QuarkType::Int;
                        else if (elementTypeName == "float") elementType = QuarkType::Float;
                        else if (elementTypeName == "double") elementType = QuarkType::Double;
                        else if (elementTypeName == "bool") elementType = QuarkType::Boolean;
                        else if (g_struct_defs_ && g_struct_defs_->find(elementTypeName) != g_struct_defs_->end()) {
                            elementType = QuarkType::Struct;
                        }
                        return TypeInfo(QuarkType::Array, expr->location, "", elementType);
                    }
                    else if (g_struct_defs_ && g_struct_defs_->find(field.second) != g_struct_defs_->end()) {
                        fieldType = QuarkType::Struct;
                    }
                    
                    if (fieldType == QuarkType::Struct) {
                        return TypeInfo(fieldType, expr->location, field.second);
                    } else {
                        return TypeInfo(fieldType, expr->location);
                    }
                }
            }
            
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("field '" + memberAccess->fieldName + "' not found in struct " + objectStructName, expr->location, file->content, ErrorCodes::SYMBOL_NOT_FOUND, (int)std::max<size_t>(1, memberAccess->fieldName.size()));
                }
            }
            throw CodeGenError("field '" + memberAccess->fieldName + "' not found in struct " + objectStructName, expr->location);
        } else if (auto *nestedAccess = dynamic_cast<MemberAccessExpr *>(memberAccess->object.get())) {
            // Handle nested member access (e.g., msg.author.id)
            TypeInfo nestedType = inferType(nestedAccess);
            if (nestedType.type != QuarkType::Struct) {
                throw CodeGenError("nested member access on non-struct type", expr->location);
            }
            
            std::string objectStructName = nestedType.structName;
            
            // Find the struct definition
            auto defIt = g_struct_defs_->find(objectStructName);
            if (defIt == g_struct_defs_->end()) {
                throw CodeGenError("struct definition not found: " + objectStructName, expr->location);
            }
            
            // Collect all fields including inherited ones
            std::vector<std::pair<std::string, std::string>> allFields;
            std::function<void(const std::string&)> collectInheritedFields = [&](const std::string& currentStructName) {
                auto currentIt = g_struct_defs_->find(currentStructName);
                if (currentIt == g_struct_defs_->end()) {
                    return;
                }
                const StructDefStmt* currentStruct = currentIt->second;
                if (!currentStruct->parentName.empty()) {
                    collectInheritedFields(currentStruct->parentName);
                }
                for (const auto& field : currentStruct->fields) {
                    allFields.push_back(field);
                }
            };
            collectInheritedFields(objectStructName);
            
            for (const auto& field : allFields) {
                if (field.first == memberAccess->fieldName) {
                    QuarkType fieldType = QuarkType::Unknown;
                    if (field.second == "int") fieldType = QuarkType::Int;
                    else if (field.second == "float") fieldType = QuarkType::Float;
                    else if (field.second == "double") fieldType = QuarkType::Double;
                    else if (field.second == "str") fieldType = QuarkType::String;
                    else if (field.second == "bool") fieldType = QuarkType::Boolean;
                    else if (field.second.size() > 2 && field.second.substr(field.second.size() - 2) == "[]") {
                        std::string elementTypeName = field.second.substr(0, field.second.size() - 2);
                        QuarkType elementType = QuarkType::Unknown;
                        if (elementTypeName == "str") elementType = QuarkType::String;
                        else if (elementTypeName == "int") elementType = QuarkType::Int;
                        else if (elementTypeName == "float") elementType = QuarkType::Float;
                        else if (elementTypeName == "double") elementType = QuarkType::Double;
                        else if (elementTypeName == "bool") elementType = QuarkType::Boolean;
                        else if (g_struct_defs_ && g_struct_defs_->find(elementTypeName) != g_struct_defs_->end()) {
                            elementType = QuarkType::Struct;
                        }
                        return TypeInfo(QuarkType::Array, expr->location, "", elementType);
                    }
                    else if (g_struct_defs_ && g_struct_defs_->find(field.second) != g_struct_defs_->end()) {
                        fieldType = QuarkType::Struct;
                    }
                    
                    if (fieldType == QuarkType::Struct) {
                        return TypeInfo(fieldType, expr->location, field.second);
                    } else {
                        return TypeInfo(fieldType, expr->location);
                    }
                }
            }
            
            throw CodeGenError("field '" + memberAccess->fieldName + "' not found in struct " + objectStructName, expr->location);
        } else {
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("complex member access expressions not yet supported", expr->location, file->content, ErrorCodes::INVALID_OPERATION, 1);
                }
            }
            throw CodeGenError("complex member access expressions not yet supported", expr->location);
        }
    }
    
    if (auto *n = dynamic_cast<NumberExprAST *>(expr)) {
                double value = n->value;
        if (value == std::floor(value) && value >= INT32_MIN && value <= INT32_MAX) {
            // This is an integer value
            return TypeInfo(QuarkType::Int, expr->location);
        } else {
                        return TypeInfo(QuarkType::Double, expr->location);
        }
    }
    
    if (auto *s = dynamic_cast<StringExprAST *>(expr))
        return TypeInfo(QuarkType::String, expr->location);
        
    if (auto *b = dynamic_cast<BoolExprAST *>(expr))
        return TypeInfo(QuarkType::Boolean, expr->location);
    
    // Handle null literal
    if (dynamic_cast<NullExprAST *>(expr))
        return TypeInfo(QuarkType::Null, expr->location);
    
        if (auto *addrOf = dynamic_cast<AddressOfExpr *>(expr)) {
        auto trimWhitespace = [](std::string &s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
            size_t start = 0;
            while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
            if (start > 0) s.erase(0, start);
        };

        auto describeOperand = [&](const TypeInfo &info) -> std::string {
            switch (info.type) {
                case QuarkType::Int: return "int";
                case QuarkType::Float: return "float";
                case QuarkType::Double: return "double";
                case QuarkType::Boolean: return "bool";
                case QuarkType::String: return "str";
                case QuarkType::Struct:
                    if (!info.structName.empty()) return info.structName;
                    break;
                case QuarkType::Pointer:
                    if (!info.pointerTypeName.empty()) return info.pointerTypeName;
                    break;
                case QuarkType::Array:
                    if (info.elementType != QuarkType::Unknown) {
                        return IntegerTypeUtils::quarkTypeToString(info.elementType);
                    }
                    break;
                default:
                    break;
            }
            return "void";
        };

        std::string pointerType = "void";
        try {
            TypeInfo operandType = inferType(addrOf->operand.get());
            pointerType = describeOperand(operandType);
        } catch (...) {
            pointerType = "void";
        }

        trimWhitespace(pointerType);
        if (pointerType.empty()) {
            pointerType = "void";
        }

                pointerType += "*";
        return TypeInfo(QuarkType::Pointer, expr->location, "", QuarkType::Unknown, 0, pointerType);
    }
    
        if (auto *deref = dynamic_cast<DereferenceExpr *>(expr)) {
        auto trimWhitespace = [](std::string &s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
            size_t start = 0;
            while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
            if (start > 0) s.erase(0, start);
        };

        try {
            TypeInfo ptrInfo = inferType(deref->operand.get());
            if (ptrInfo.type == QuarkType::Pointer && !ptrInfo.pointerTypeName.empty()) {
                std::string pointee = ptrInfo.pointerTypeName;
                trimWhitespace(pointee);
                if (!pointee.empty() && pointee.back() == '*') {
                    pointee.pop_back();
                    trimWhitespace(pointee);
                }

                if (pointee.empty()) {
                    return TypeInfo(QuarkType::Int, expr->location);
                }

                                if (pointee.find('*') != std::string::npos) {
                    return TypeInfo(QuarkType::Pointer, expr->location, "", QuarkType::Unknown, 0, pointee);
                }

                QuarkType baseType = IntegerTypeUtils::stringToQuarkType(pointee);
                if (baseType == QuarkType::Pointer) {
                                        return TypeInfo(QuarkType::Pointer, expr->location, "", QuarkType::Unknown, 0, pointee);
                }
                if (baseType == QuarkType::String) {
                    return TypeInfo(QuarkType::String, expr->location);
                }
                if (baseType != QuarkType::Unknown && baseType != QuarkType::Struct) {
                    return TypeInfo(baseType, expr->location);
                }

                if (g_struct_defs_ && g_struct_defs_->find(pointee) != g_struct_defs_->end()) {
                    return TypeInfo(QuarkType::Struct, expr->location, pointee);
                }

                return TypeInfo(QuarkType::Int, expr->location);
            }
        } catch (...) {
                    }

        return TypeInfo(QuarkType::Int, expr->location);
    }
    
        if (auto *arrayLiteral = dynamic_cast<ArrayLiteralExpr *>(expr)) {
        if (arrayLiteral->elements.empty()) {
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("Cannot infer type for empty array literal", expr->location, file->content, ErrorCodes::INVALID_TYPE, 2);
                }
            }
            throw CodeGenError("Cannot infer type for empty array literal", expr->location);
        }
        
                TypeInfo elementType = inferType(arrayLiteral->elements[0].get());
        
                return TypeInfo(QuarkType::Array, expr->location, "", elementType.type, arrayLiteral->elements.size());
    }
    
    // Map literal
    if (auto *mapLiteral = dynamic_cast<MapLiteralExpr *>(expr)) {
        return TypeInfo(QuarkType::Map, expr->location);
    }
    
    // Array/Map access expression: arr[index] or map[key]
    if (auto *arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr)) {
        TypeInfo arrayType = inferType(arrayAccess->array.get());
        if (arrayType.type == QuarkType::Array) {
            // Return the element type
            return TypeInfo(arrayType.elementType, expr->location);
        } else if (arrayType.type == QuarkType::Map) {
            // Map access always returns string
            return TypeInfo(QuarkType::String, expr->location);
        } else if (arrayType.type == QuarkType::String) {
                        return TypeInfo(QuarkType::Int, expr->location);
        } else {
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("Cannot index non-array/map type", expr->location, file->content, ErrorCodes::INVALID_OPERATION, 1);
                }
            }
            throw CodeGenError("Cannot index non-array/map type", expr->location);
        }
    }

        if (auto *cast = dynamic_cast<CastExpr *>(expr)) {
        if (!cast->targetTypeName.empty() && cast->targetTypeName.find('*') != std::string::npos) {
            return TypeInfo(QuarkType::Pointer, expr->location, "", QuarkType::Unknown, 0, cast->targetTypeName);
        }
        if (cast->targetTypeName == "int") return TypeInfo(QuarkType::Int, expr->location);
        if (cast->targetTypeName == "float") return TypeInfo(QuarkType::Float, expr->location);
        if (cast->targetTypeName == "double") return TypeInfo(QuarkType::Double, expr->location);
        if (cast->targetTypeName == "bool") return TypeInfo(QuarkType::Boolean, expr->location);
        if (cast->targetTypeName == "str") return TypeInfo(QuarkType::String, expr->location);
        return TypeInfo(QuarkType::Unknown, expr->location);
    }
        
    if (auto *v = dynamic_cast<VariableExprAST *>(expr))
    {
        auto it = variableTypes_.find(v->name);
        if (it != variableTypes_.end())
            return it->second;
        
        // Check global variables
        if (g_named_types_) {
            auto git = g_named_types_->find(v->name);
            if (git != g_named_types_->end())
            {
                QuarkType type = QuarkType::Unknown;
                if (git->second == int32_t_) type = QuarkType::Int;
                else if (git->second == int8ptr_t_) type = QuarkType::String;
                else if (git->second == bool_t_) type = QuarkType::Boolean;
                else if (g_struct_types_) {
                                        for (const auto& structPair : *g_struct_types_) {
                        // Direct struct value
                        if (structPair.second == git->second) {
                            return TypeInfo(QuarkType::Struct, expr->location, structPair.first);
                        }
                        // Pointer to struct
                        if (LLVMPointerType(structPair.second, 0) == git->second) {
                            return TypeInfo(QuarkType::Struct, expr->location, structPair.first);
                        }
                    }
                }
                return TypeInfo(type, expr->location);
            }
        }
        
        auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
            auto file = sm->getFile(expr->location.filename);
            if (file) {
                throw EnhancedCodeGenError("undefined variable '" + v->name + "'", expr->location, file->content, ErrorCodes::SYMBOL_NOT_FOUND, (int)std::max<size_t>(1, v->name.size()));
            }
        }
        throw CodeGenError("undefined variable '" + v->name + "'", expr->location);
    }
    
    if (auto *bin = dynamic_cast<BinaryExprAST *>(expr))
    {
        TypeInfo lhsType = inferType(bin->lhs.get());
        TypeInfo rhsType = inferType(bin->rhs.get());
        
        switch (bin->op)
        {
        case '+':
                        if (lhsType.type == QuarkType::String || rhsType.type == QuarkType::String)
                return TypeInfo(QuarkType::String, expr->location);
                        if (IntegerTypeUtils::isNumericType(lhsType.type) && IntegerTypeUtils::isNumericType(rhsType.type)) {
                if (lhsType.type == QuarkType::Double || rhsType.type == QuarkType::Double)
                    return TypeInfo(QuarkType::Double, expr->location);
                if (lhsType.type == QuarkType::Float || rhsType.type == QuarkType::Float)
                    return TypeInfo(QuarkType::Float, expr->location);
                // both integers
                return TypeInfo(QuarkType::Int, expr->location);
            }
            break;
        case '-':
        case '*':
        case '/':
        case '%':
                        if (IntegerTypeUtils::isNumericType(lhsType.type) && IntegerTypeUtils::isNumericType(rhsType.type)) {
                if (lhsType.type == QuarkType::Double || rhsType.type == QuarkType::Double)
                    return TypeInfo(QuarkType::Double, expr->location);
                if (lhsType.type == QuarkType::Float || rhsType.type == QuarkType::Float)
                    return TypeInfo(QuarkType::Float, expr->location);
                return TypeInfo(QuarkType::Int, expr->location);
            }
                        checkTypeCompatibility(QuarkType::Int, lhsType.type, lhsType.location, "arithmetic operation");
            checkTypeCompatibility(QuarkType::Int, rhsType.type, rhsType.location, "arithmetic operation");
            return TypeInfo(QuarkType::Int, expr->location);
        case '=':
        case 'n': // !=
        case '<':
        case '>':
        case 'l': // <=
        case 'g': // >=
            return TypeInfo(QuarkType::Boolean, expr->location);
        case '&': // &&
        case '|': // ||
            checkTypeCompatibility(QuarkType::Boolean, lhsType.type, lhsType.location, "logical operation");
            checkTypeCompatibility(QuarkType::Boolean, rhsType.type, rhsType.location, "logical operation");
            return TypeInfo(QuarkType::Boolean, expr->location);
        }
    }
    
    // Handle method calls
    if (auto *methodCall = dynamic_cast<MethodCallExpr *>(expr)) {
                TypeInfo objType = inferType(methodCall->object.get());
        if (objType.type == QuarkType::Array) {
            if (methodCall->methodName == "count" || methodCall->methodName == "length") {
                return TypeInfo(QuarkType::Int, expr->location);
            }
            if (methodCall->methodName == "push" || methodCall->methodName == "slice") {
                return TypeInfo(QuarkType::Array, expr->location, "", objType.elementType);
            }
            if (methodCall->methodName == "free") {
                return TypeInfo(QuarkType::Void, expr->location);
            }
            // Unknown array method
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("Unknown array method: " + methodCall->methodName, expr->location, file->content, ErrorCodes::FUNCTION_NOT_FOUND, (int)std::max<size_t>(1, methodCall->methodName.size()));
                }
            }
            throw CodeGenError("Unknown array method: " + methodCall->methodName, expr->location);
        }

        // Special case for str.split() which returns str[]
        if (methodCall->methodName == "split") {
            return TypeInfo(QuarkType::Array, expr->location, "", QuarkType::String);
        }

        std::string builtinName = "str_" + methodCall->methodName;
        
        if (builtinFunctions_ && builtinFunctions_->isBuiltin(builtinName)) {
            const BuiltinFunction* builtin = builtinFunctions_->getBuiltin(builtinName);
            if (builtin) {
                                if (builtin->returnType == int32_t_) {
                    return TypeInfo(QuarkType::Int, expr->location);
                } else if (builtin->returnType == LLVMFloatTypeInContext(ctx_)) {
                    return TypeInfo(QuarkType::Float, expr->location);
                } else if (builtin->returnType == LLVMDoubleTypeInContext(ctx_)) {
                    return TypeInfo(QuarkType::Double, expr->location);
                } else if (builtin->returnType == int8ptr_t_) {
                    return TypeInfo(QuarkType::String, expr->location);
                } else if (builtin->returnType == bool_t_) {
                    return TypeInfo(QuarkType::Boolean, expr->location);
                }
            }
        }
        
                if (objType.type == QuarkType::Struct) {
            std::string actualStructType = objType.structName;

                        if (actualStructType.empty()) {
                if (auto *varExpr = dynamic_cast<VariableExprAST *>(methodCall->object.get())) {
                    auto vit = variableTypes_.find(varExpr->name);
                    if (vit != variableTypes_.end() && vit->second.type == QuarkType::Struct) {
                        actualStructType = vit->second.structName;
                    } else if (g_named_types_) {
                        auto git = g_named_types_->find(varExpr->name);
                        if (git != g_named_types_->end() && g_struct_types_) {
                            for (const auto &p : *g_struct_types_) {
                                if (p.second == git->second) { actualStructType = p.first; break; }
                            }
                        }
                    }
                }
            }

            if (!actualStructType.empty()) {
                                std::string targetStruct = actualStructType;
                std::string mangled = targetStruct + "::" + methodCall->methodName;

                auto findIt = functionTypes_.find(mangled);
                // Walk parent chain if needed
                while (findIt == functionTypes_.end()) {
                    if (!g_struct_defs_) break;
                    auto defIt = g_struct_defs_->find(targetStruct);
                    if (defIt == g_struct_defs_->end() || defIt->second->parentName.empty()) break;
                    targetStruct = defIt->second->parentName;
                    mangled = targetStruct + "::" + methodCall->methodName;
                    findIt = functionTypes_.find(mangled);
                }

                if (findIt != functionTypes_.end()) {
                    return findIt->second;
                }
            }
        }

                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
            auto file = sm->getFile(expr->location.filename);
            if (file) {
                throw EnhancedCodeGenError("Unknown method call: " + methodCall->methodName, expr->location, file->content, ErrorCodes::FUNCTION_NOT_FOUND, (int)std::max<size_t>(1, methodCall->methodName.size()));
            }
        }
        throw CodeGenError("Unknown method call: " + methodCall->methodName, expr->location);
    }
    
    if (auto *call = dynamic_cast<CallExprAST *>(expr))
    {
        if (verbose_)
            printf("[codegen] inferType: checking function call '%s'\n", call->callee.c_str());
            
                if (builtinFunctions_ && builtinFunctions_->isBuiltin(call->callee)) {
            const BuiltinFunction* bi = builtinFunctions_->getBuiltin(call->callee);
            if (bi) {
                                if (call->callee == "clamp" || call->callee == "min" || call->callee == "max") {
                    bool allInt = true;
                    for (const auto& arg : call->args) {
                        TypeInfo t = inferType(arg.get());
                        if (!IntegerTypeUtils::isIntegerType(t.type)) { allInt = false; break; }
                    }
                    if (allInt) return TypeInfo(QuarkType::Int, expr->location);
                    return TypeInfo(QuarkType::Double, expr->location);
                }
                                if (call->callee == "array_slice" || call->callee == "array_push") {
                    if (!call->args.empty()) {
                        TypeInfo arrT = inferType(call->args[0].get());
                        if (arrT.type == QuarkType::Array) {
                            return TypeInfo(QuarkType::Array, expr->location, "", arrT.elementType);
                        }
                    }
                    return TypeInfo(QuarkType::Array, expr->location);
                }
                if (call->callee == "array_length") {
                    return TypeInfo(QuarkType::Int, expr->location);
                }
                if (call->callee == "array_free") {
                    return TypeInfo(QuarkType::Void, expr->location);
                }
                // str_split returns str[]
                if (call->callee == "str_split") {
                    return TypeInfo(QuarkType::Array, expr->location, "", QuarkType::String, 0);
                }

                if (call->callee == "map_new") {
                    return TypeInfo(QuarkType::Map, expr->location);
                }
                if (call->callee == "map_get") {
                    return TypeInfo(QuarkType::String, expr->location);
                }
                if (call->callee == "map_has") {
                    return TypeInfo(QuarkType::Boolean, expr->location);
                }
                if (call->callee == "map_len") {
                    return TypeInfo(QuarkType::Int, expr->location);
                }
                if (call->callee == "map_set" || call->callee == "map_remove" || call->callee == "map_free") {
                    return TypeInfo(QuarkType::Void, expr->location);
                }

                                if (bi->returnType == int32_t_) {
                    return TypeInfo(QuarkType::Int, expr->location);
                } else if (bi->returnType == LLVMFloatTypeInContext(ctx_)) {
                    return TypeInfo(QuarkType::Float, expr->location);
                } else if (bi->returnType == LLVMDoubleTypeInContext(ctx_)) {
                    return TypeInfo(QuarkType::Double, expr->location);
                } else if (bi->returnType == int8ptr_t_) {
                    return TypeInfo(QuarkType::String, expr->location);
                } else if (bi->returnType == bool_t_) {
                    return TypeInfo(QuarkType::Boolean, expr->location);
                } else {
                    return TypeInfo(QuarkType::Unknown, expr->location);
                }
            }
        }

        if (call->callee == "readline")
            return TypeInfo(QuarkType::String, expr->location);
        
        auto it = functionTypes_.find(call->callee);
        if (it != functionTypes_.end()) {
            if (verbose_)
                printf("[codegen] inferType: found function '%s' with registered type\n", call->callee.c_str());
            return it->second;
        } else {
            if (verbose_)
                printf("[codegen] inferType: function '%s' not found in functionTypes_\n", call->callee.c_str());
        }
    }
    
    if (auto *staticCall = dynamic_cast<StaticCallExpr *>(expr))
    {
        if (verbose_)
            printf("[codegen] inferType: checking static method call '%s::%s'\n", staticCall->structName.c_str(), staticCall->methodName.c_str());
        
        std::string actualStructType = staticCall->structName;
        
        auto varIt = variableTypes_.find(staticCall->structName);
        if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::Struct) {
            actualStructType = varIt->second.structName;
            if (verbose_)
                printf("[codegen] inferType: resolved variable '%s' to struct type '%s'\n", 
                       staticCall->structName.c_str(), actualStructType.c_str());
        } else if (g_named_types_) {
            auto globalIt = g_named_types_->find(staticCall->structName);
            if (globalIt != g_named_types_->end()) {
                if (g_struct_types_) {
                    for (const auto& structPair : *g_struct_types_) {
                        if (structPair.second == globalIt->second) {
                            actualStructType = structPair.first;
                            if (verbose_)
                                printf("[codegen] inferType: resolved global variable '%s' to struct type '%s'\n", 
                                       staticCall->structName.c_str(), actualStructType.c_str());
                            break;
                        }
                    }
                }
            }
        }
        
        if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::Array) {
            if (staticCall->methodName == "length" || staticCall->methodName == "count") {
                return TypeInfo(QuarkType::Int, staticCall->location);
            }
            if (staticCall->methodName == "slice" || staticCall->methodName == "push") {
                return varIt->second;
            }
            if (staticCall->methodName == "pop" || staticCall->methodName == "free") {
                return TypeInfo(QuarkType::Void, staticCall->location);
            }
        }
        
        if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::String) {
            if (staticCall->methodName == "length") {
                return TypeInfo(QuarkType::Int, staticCall->location);
            }
            if (staticCall->methodName == "slice" || staticCall->methodName == "replace") {
                return TypeInfo(QuarkType::String, staticCall->location);
            }
            if (staticCall->methodName == "find") {
                return TypeInfo(QuarkType::Boolean, staticCall->location);
            }
            if (staticCall->methodName == "split") {
                TypeInfo arrType(QuarkType::Array, staticCall->location);
                arrType.elementType = QuarkType::String;
                return arrType;
            }
        }
        
        std::string mangledName = actualStructType + "::" + staticCall->methodName;
        auto it = functionTypes_.find(mangledName);
        if (it != functionTypes_.end()) {
            if (verbose_)
                printf("[codegen] inferType: found static method '%s' with registered type\n", mangledName.c_str());
            return it->second;
        } else {
            if (verbose_)
                printf("[codegen] inferType: static method '%s' not found in functionTypes_\n", mangledName.c_str());
        }
    }
    
    return TypeInfo(QuarkType::Unknown, expr->location);
}

void ExpressionCodeGen::checkTypeCompatibility(QuarkType expected, QuarkType actual, const SourceLocation &loc, const std::string &context)
{
        if (actual == QuarkType::Unknown)
        return;

        if (expected == QuarkType::Int && IntegerTypeUtils::isIntegerType(actual))
        return;

        if (IntegerTypeUtils::isFloatingType(expected) && IntegerTypeUtils::isFloatingType(actual))
        return;

        if (IntegerTypeUtils::isNumericType(expected) && IntegerTypeUtils::isNumericType(actual))
        return;

    // Direct match
    if (expected == actual)
        return;

            auto typeToStr = [](QuarkType t) -> std::string {
        if (IntegerTypeUtils::isIntegerType(t)) return std::string("integer");
        if (IntegerTypeUtils::isFloatingType(t)) return std::string("floating-point");
        switch (t) {
            case QuarkType::String: return "string";
            case QuarkType::Map: return "map";
            case QuarkType::Boolean: return "boolean";
            case QuarkType::Void: return "void";
            case QuarkType::Array: return "array";
            default: return "unknown";
        }
    };

    std::string expectedStr = typeToStr(expected);
    std::string actualStr = typeToStr(actual);
    auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
        auto file = sm->getFile(loc.filename);
        if (file) {
            throw EnhancedCodeGenError("type mismatch in " + context + ": expected " + expectedStr + ", got " + actualStr, loc, file->content, ErrorCodes::TYPE_MISMATCH, (int)std::max<size_t>(1, context.size()));
        }
    }
    throw CodeGenError("type mismatch in " + context + ": expected " + expectedStr + ", got " + actualStr, loc);
}

QuarkType ExpressionCodeGen::getVariableType(const std::string &name)
{
    auto it = variableTypes_.find(name);
    if (it != variableTypes_.end())
        return it->second.type;
    return QuarkType::Unknown;
}

void ExpressionCodeGen::declareVariable(const std::string &name, QuarkType type, const SourceLocation &loc, const std::string &structName, const std::string &pointerType)
{
    if (type == QuarkType::Struct && !structName.empty()) {
        variableTypes_[name] = TypeInfo(type, loc, structName);
    } else if (type == QuarkType::Pointer) {
        variableTypes_[name] = TypeInfo(type, loc, "", QuarkType::Unknown, 0, pointerType);
    } else {
        variableTypes_[name] = TypeInfo(type, loc);
    }
}

void ExpressionCodeGen::declareVariable(const std::string &name, QuarkType type, const SourceLocation &loc, QuarkType elementType, size_t arraySize)
{
    if (type == QuarkType::Array) {
        variableTypes_[name] = TypeInfo(type, loc, "", elementType, arraySize);
    } else {
                variableTypes_[name] = TypeInfo(type, loc);
    }
}

void ExpressionCodeGen::declareFunctionType(const std::string &name, QuarkType returnType, const SourceLocation &loc, const std::string &structName)
{
    functionTypes_[name] = TypeInfo(returnType, loc, structName);
    if (verbose_)
        printf("[codegen] registered function '%s' with return type\n", name.c_str());
}

void ExpressionCodeGen::declareFunctionType(const std::string &name, QuarkType returnType, const SourceLocation &loc, QuarkType elementType, size_t arraySize)
{
    functionTypes_[name] = TypeInfo(returnType, loc, "", elementType, arraySize);
    if (verbose_)
        printf("[codegen] registered function '%s' with array return type\n", name.c_str());
}

std::optional<double> ExpressionCodeGen::evalConst(ExprAST *expr)
{
    if (auto *n = dynamic_cast<NumberExprAST *>(expr))
        return n->value;
    if (auto *b = dynamic_cast<BoolExprAST *>(expr))
        return b->value ? 1.0 : 0.0;
    if (auto *v = dynamic_cast<VariableExprAST *>(expr))
    {
        if (g_const_values_) {
            auto it = g_const_values_->find(v->name);
            if (it != g_const_values_->end())
                return it->second;
        }
        return std::nullopt;
    }
    if (auto *c = dynamic_cast<CallExprAST *>(expr))
    {
                return std::nullopt;
    }
    if (auto *b = dynamic_cast<BinaryExprAST *>(expr))
    {
        auto L = evalConst(b->lhs.get());
        auto R = evalConst(b->rhs.get());
        if (!L || !R)
            return std::nullopt;
        double l = *L;
        double r = *R;
        switch (b->op)
        {
        case '+': return l + r;
        case '-': return l - r;
        case '*': return l * r;
        case '/': return r != 0.0 ? l / r : std::nan("");
        case '%': return r != 0.0 ? std::fmod(l, r) : std::nan("");
        case '=': return (l == r) ? 1.0 : 0.0; // ==
        case 'n': return (l != r) ? 1.0 : 0.0; // !=
        case '<': return (l <  r) ? 1.0 : 0.0; // <
        case '>': return (l >  r) ? 1.0 : 0.0; // >
        case 'l': return (l <= r) ? 1.0 : 0.0; // <=
        case 'g': return (l >= r) ? 1.0 : 0.0; // >=
        case '&': return ((l != 0.0) && (r != 0.0)) ? 1.0 : 0.0; // &&
        case '|': return ((l != 0.0) || (r != 0.0)) ? 1.0 : 0.0; // ||
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ExpressionCodeGen::evalConstString(ExprAST *expr)
{
    if (auto *s = dynamic_cast<StringExprAST *>(expr))
        return s->value;
    if (auto *n = dynamic_cast<NumberExprAST *>(expr))
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", n->value);
        return std::string(buf);
    }
    if (auto *b = dynamic_cast<BinaryExprAST *>(expr))
    {
        if (b->op != '+')
            return std::nullopt;
        auto L = evalConstString(b->lhs.get());
        auto R = evalConstString(b->rhs.get());
        if (!L || !R)
            return std::nullopt;
        return *L + *R;
    }
    return std::nullopt;
}

LLVMValueRef ExpressionCodeGen::genExpr(ExprAST *expr)
{
        if (auto *cast = dynamic_cast<CastExpr *>(expr)) {
        // Generate operand first
        LLVMValueRef val = genExpr(cast->operand.get());
        TypeInfo srcT;
        try { srcT = inferType(cast->operand.get()); } catch (...) { srcT = TypeInfo(QuarkType::Unknown, expr->location); }

                QuarkType dstQt = IntegerTypeUtils::stringToQuarkType(cast->targetTypeName);
        LLVMTypeRef dstTy = nullptr;
        if (dstQt == QuarkType::Pointer) {
            dstTy = mapPointerType(cast->targetTypeName);
        } else {
            dstTy = quarkTypeToLLVMType(dstQt);
        }

        if (dstQt == QuarkType::Pointer) {
            if (!dstTy) {
                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                    auto file = sm->getFile(expr->location.filename);
                    if (file) {
                        throw EnhancedCodeGenError("unsupported pointer target type '" + cast->targetTypeName + "'", expr->location, file->content, ErrorCodes::INVALID_TYPE, (int)std::max<size_t>(1, cast->targetTypeName.size()));
                    }
                }
                throw CodeGenError("unsupported pointer target type '" + cast->targetTypeName + "'", expr->location);
            }

            LLVMTypeRef valTy = LLVMTypeOf(val);
            LLVMTypeRef intptrTy = LLVMInt64TypeInContext(ctx_);

            if (LLVMGetTypeKind(valTy) == LLVMPointerTypeKind) {
                if (valTy == dstTy) {
                    return val;
                }
                return LLVMBuildPointerCast(builder_, val, dstTy, "ptr_cast");
            }

            if (LLVMGetTypeKind(valTy) == LLVMIntegerTypeKind) {
                unsigned width = LLVMGetIntTypeWidth(valTy);
                LLVMValueRef widened = val;
                if (width < 64) {
                    widened = LLVMBuildZExt(builder_, val, intptrTy, "int_to_ptr_zext");
                } else if (width > 64) {
                    widened = LLVMBuildTrunc(builder_, val, intptrTy, "int_to_ptr_trunc");
                }
                return LLVMBuildIntToPtr(builder_, widened, dstTy, "int_to_ptr");
            }

            if (LLVMGetTypeKind(valTy) == LLVMDoubleTypeKind || LLVMGetTypeKind(valTy) == LLVMFloatTypeKind) {
                LLVMValueRef asDouble = val;
                if (LLVMGetTypeKind(valTy) == LLVMFloatTypeKind) {
                    asDouble = LLVMBuildFPExt(builder_, val, double_t_, "float_to_double_ptr");
                }
                LLVMValueRef asInt = LLVMBuildFPToUI(builder_, asDouble, intptrTy, "fp_to_intptr");
                return LLVMBuildIntToPtr(builder_, asInt, dstTy, "double_to_ptr");
            }

            if (srcT.type == QuarkType::Boolean) {
                LLVMValueRef boolVal = val;
                if (!(LLVMGetTypeKind(valTy) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(valTy) == 1)) {
                    LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(val), 0, 0);
                    boolVal = LLVMBuildICmp(builder_, LLVMIntNE, val, zero, "bool_norm");
                }
                LLVMValueRef widened = LLVMBuildZExt(builder_, boolVal, intptrTy, "bool_to_intptr");
                return LLVMBuildIntToPtr(builder_, widened, dstTy, "bool_to_ptr");
            }

            if (srcT.type == QuarkType::Pointer) {
                return LLVMBuildPointerCast(builder_, val, dstTy, "ptr_cast_unknown");
            }

            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("unsupported cast to pointer type", expr->location, file->content, ErrorCodes::INVALID_TYPE, (int)std::max<size_t>(1, cast->targetTypeName.size()));
                }
            }
            throw CodeGenError("unsupported cast to pointer type", expr->location);
        }

                if (dstQt == QuarkType::String) {
                        if (srcT.type == QuarkType::String) {
                if (LLVMTypeOf(val) != int8ptr_t_) {
                    return LLVMBuildPointerCast(builder_, val, int8ptr_t_, "to_str");
                }
                return val;
            }
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("cannot cast to str from non-string type", expr->location, file->content, ErrorCodes::INVALID_TYPE, 1);
                }
            }
            throw CodeGenError("cannot cast to str from non-string type", expr->location);
        }

                if (dstQt == QuarkType::Boolean) {
            if (IntegerTypeUtils::isIntegerType(srcT.type)) {
                LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
                                LLVMValueRef i32v = (LLVMTypeOf(val) == int32_t_) ? val : LLVMBuildZExt(builder_, val, int32_t_, "to_i32");
                return LLVMBuildICmp(builder_, LLVMIntNE, i32v, zero, "int_to_bool");
            } else if (srcT.type == QuarkType::Float) {
                LLVMValueRef zero = LLVMConstReal(float_t_, 0.0);
                return LLVMBuildFCmp(builder_, LLVMRealONE, val, zero, "float_to_bool");
            } else if (srcT.type == QuarkType::Double) {
                LLVMValueRef zero = LLVMConstReal(double_t_, 0.0);
                return LLVMBuildFCmp(builder_, LLVMRealONE, val, zero, "double_to_bool");
            } else if (srcT.type == QuarkType::Boolean) {
                return val;
            }
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("unsupported cast to bool", expr->location, file->content, ErrorCodes::INVALID_TYPE, 4);
                }
            }
            throw CodeGenError("unsupported cast to bool", expr->location);
        }

                if (IntegerTypeUtils::isIntegerType(dstQt)) {
            if (IntegerTypeUtils::isIntegerType(srcT.type)) {
                // Normalize to i32
                if (LLVMTypeOf(val) != int32_t_) {
                    return LLVMBuildZExt(builder_, val, int32_t_, "int_norm_i32");
                }
                return val;
            } else if (srcT.type == QuarkType::Float) {
                return LLVMBuildFPToSI(builder_, val, int32_t_, "float_to_int");
            } else if (srcT.type == QuarkType::Double) {
                return LLVMBuildFPToSI(builder_, val, int32_t_, "double_to_int");
            } else if (srcT.type == QuarkType::Boolean) {
                // zero-extend i1 to i32
                if (LLVMTypeOf(val) == bool_t_) return LLVMBuildZExt(builder_, val, int32_t_, "bool_to_int");
                                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(val), 0, 0);
                LLVMValueRef asBool = LLVMBuildICmp(builder_, LLVMIntNE, val, zero, "to_bool");
                return LLVMBuildZExt(builder_, asBool, int32_t_, "bool_to_int2");
            }
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("unsupported cast to int", expr->location, file->content, ErrorCodes::INVALID_TYPE, 3);
                }
            }
            throw CodeGenError("unsupported cast to int", expr->location);
        }

                if (dstQt == QuarkType::Float) {
            if (srcT.type == QuarkType::Float) return val;
            if (srcT.type == QuarkType::Double) return LLVMBuildFPTrunc(builder_, val, float_t_, "double_to_float");
            if (IntegerTypeUtils::isIntegerType(srcT.type)) {
                // Ensure i32 width
                LLVMValueRef i32v = (LLVMTypeOf(val) == int32_t_) ? val : LLVMBuildZExt(builder_, val, int32_t_, "to_i32");
                return LLVMBuildSIToFP(builder_, i32v, float_t_, "int_to_float");
            }
            if (srcT.type == QuarkType::Boolean) {
                LLVMValueRef i32v = LLVMBuildZExt(builder_, val, int32_t_, "bool_i32");
                return LLVMBuildSIToFP(builder_, i32v, float_t_, "bool_to_float");
            }
            if (srcT.type == QuarkType::Pointer) {
                LLVMValueRef asInt = LLVMBuildPtrToInt(builder_, val, LLVMInt64TypeInContext(ctx_), "ptr_to_i64_for_float");
                return LLVMBuildUIToFP(builder_, asInt, float_t_, "ptr_to_float");
            }
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("unsupported cast to float", expr->location, file->content, ErrorCodes::INVALID_TYPE, 5);
                }
            }
            throw CodeGenError("unsupported cast to float", expr->location);
        }

        if (dstQt == QuarkType::Double) {
            if (srcT.type == QuarkType::Double) return val;
            if (srcT.type == QuarkType::Float) return LLVMBuildFPExt(builder_, val, double_t_, "float_to_double");
            if (IntegerTypeUtils::isIntegerType(srcT.type)) {
                LLVMValueRef i32v = (LLVMTypeOf(val) == int32_t_) ? val : LLVMBuildZExt(builder_, val, int32_t_, "to_i32");
                return LLVMBuildSIToFP(builder_, i32v, double_t_, "int_to_double");
            }
            if (srcT.type == QuarkType::Boolean) {
                LLVMValueRef i32v = LLVMBuildZExt(builder_, val, int32_t_, "bool_i32");
                return LLVMBuildSIToFP(builder_, i32v, double_t_, "bool_to_double");
            }
            if (srcT.type == QuarkType::Pointer) {
                LLVMValueRef asInt = LLVMBuildPtrToInt(builder_, val, LLVMInt64TypeInContext(ctx_), "ptr_to_i64");
                return LLVMBuildUIToFP(builder_, asInt, double_t_, "ptr_to_double");
            }
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("unsupported cast to double", expr->location, file->content, ErrorCodes::INVALID_TYPE, 6);
                }
            }
            throw CodeGenError("unsupported cast to double", expr->location);
        }

                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
            auto file = sm->getFile(expr->location.filename);
            if (file) {
                throw EnhancedCodeGenError("unsupported cast target type '" + cast->targetTypeName + "'", expr->location, file->content, ErrorCodes::INVALID_TYPE, (int)std::max<size_t>(1, cast->targetTypeName.size()));
            }
        }
        throw CodeGenError("unsupported cast target type '" + cast->targetTypeName + "'", expr->location);
    }

    // Handle struct literals
    if (auto *structLiteral = dynamic_cast<StructLiteralExpr *>(expr)) {
        return genStructLiteral(structLiteral);
    }
    
    // Handle member access
    if (auto *memberAccess = dynamic_cast<MemberAccessExpr *>(expr)) {
        return genMemberAccess(memberAccess);
    }
    
        if (auto *methodCall = dynamic_cast<MethodCallExpr *>(expr)) {
        return genMethodCall(methodCall);
    }
    
    // Handle static method calls
    if (auto *staticCall = dynamic_cast<StaticCallExpr *>(expr)) {
        return genStaticCall(staticCall);
    }
    
        if (auto *addrOf = dynamic_cast<AddressOfExpr *>(expr)) {
        return genAddressOf(addrOf);
    }
    
        if (auto *deref = dynamic_cast<DereferenceExpr *>(expr)) {
        return genDereference(deref);
    }
    
        if (auto *range = dynamic_cast<RangeExpr *>(expr)) {
        return genRange(range);
    }
    
    
    if (auto *arrayLiteral = dynamic_cast<ArrayLiteralExpr *>(expr)) {
        return genArrayLiteral(arrayLiteral);
    }
    
    if (auto *mapLiteral = dynamic_cast<MapLiteralExpr *>(expr)) {
        return genMapLiteral(mapLiteral);
    }
    
    if (auto *arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr)) {
        return genArrayAccess(arrayAccess);
    }    if (auto *b = dynamic_cast<BoolExprAST *>(expr)) {
        return LLVMConstInt(bool_t_, b->value ? 1 : 0, 0);
    }
    // Handle null literal - return null pointer (void*)
    if (dynamic_cast<NullExprAST *>(expr)) {
        return LLVMConstPointerNull(int8ptr_t_);
    }
    if (auto *s = dynamic_cast<StringExprAST *>(expr))
    {
        const std::string& strValue = s->value;
                return LLVMBuildGlobalStringPtr(builder_, strValue.c_str(), "str_literal");
    }
    if (auto *n = dynamic_cast<NumberExprAST *>(expr))
    {
        double value = n->value;
                if (value == std::floor(value) && value >= INT32_MIN && value <= INT32_MAX) {
            // This is an integer value
            return LLVMConstInt(int32_t_, static_cast<long long>(value), 0);
        } else {
                        return LLVMConstReal(double_t_, value);
        }
    }
    if (auto *u = dynamic_cast<UnaryExprAST *>(expr))
    {
        LLVMValueRef operand = genExpr(u->operand.get());
        
        if (u->op == '-')
        {
                        if (auto *numExpr = dynamic_cast<NumberExprAST *>(u->operand.get()))
            {
                // Direct number negation
                double value = numExpr->value;
                if (value == std::floor(value) && value >= INT32_MIN && value <= INT32_MAX) {
                    // Integer negation
                    LLVMValueRef intVal = LLVMConstInt(int32_t_, (int32_t)value, 0);
                    return LLVMBuildNeg(builder_, intVal, "neg");
                } else {
                    // Floating-point negation
                    LLVMValueRef floatVal = LLVMConstReal(double_t_, value);
                    return LLVMBuildFNeg(builder_, floatVal, "fneg");
                }
            }
            else
            {
                                TypeInfo operandType;
                try {
                    operandType = inferType(u->operand.get());
                } catch (...) {
                    operandType = TypeInfo(QuarkType::Unknown, {});
                }
                
                if (IntegerTypeUtils::isFloatingType(operandType.type)) {
                    // Floating-point negation
                    LLVMValueRef floatVal = genExpr(u->operand.get());
                    return LLVMBuildFNeg(builder_, floatVal, "fneg");
                } else {
                                        LLVMValueRef intVal = genExprInt(u->operand.get());
                    return LLVMBuildNeg(builder_, intVal, "neg");
                }
            }
        }
        else if (u->op == '!')
        {
                        LLVMValueRef boolVal = genExprBool(u->operand.get());
            return LLVMBuildNot(builder_, boolVal, "not");
        }
    }
    if (auto *b = dynamic_cast<BinaryExprAST *>(expr))
    {
                if (b->op == '&' || b->op == '|' || b->op == '=' || b->op == 'n' ||
            b->op == '<' || b->op == '>' || b->op == 'l' || b->op == 'g')
        {
            return genExprBool(expr);
        }

        if (b->op == '=' || b->op == '!')
        {
            LLVMValueRef lhs = genExpr(b->lhs.get());
            LLVMValueRef rhs = genExpr(b->rhs.get());
            
                                    if (auto *lhsVar = dynamic_cast<VariableExprAST *>(b->lhs.get()))
            {
                if (g_named_values_ && g_named_types_) {
                    auto it = g_named_values_->find(lhsVar->name);
                    if (it != g_named_values_->end())
                    {
                        auto typeIt = g_named_types_->find(lhsVar->name);
                        if (typeIt != g_named_types_->end() && typeIt->second == int8ptr_t_)
                        {
                                                        auto pit = function_params_.find(lhsVar->name);
                            bool isDirectParam = (pit != function_params_.end() && pit->second);
                            
                            if (isDirectParam) {
                                lhs = it->second; // Use parameter directly
                            } else {
                                lhs = LLVMBuildLoad2(builder_, int8ptr_t_, it->second, (lhsVar->name + ".load").c_str());
                            }
                        }
                    }
                }
            }
            
            if (auto *rhsVar = dynamic_cast<VariableExprAST *>(b->rhs.get()))
            {
                if (g_named_values_ && g_named_types_) {
                    auto it = g_named_values_->find(rhsVar->name);
                    if (it != g_named_values_->end())
                    {
                        auto typeIt = g_named_types_->find(rhsVar->name);
                        if (typeIt != g_named_types_->end() && typeIt->second == int8ptr_t_)
                        {
                                                        auto pit = function_params_.find(rhsVar->name);
                            bool isDirectParam = (pit != function_params_.end() && pit->second);
                            
                            if (isDirectParam) {
                                rhs = it->second; // Use parameter directly
                            } else {
                                rhs = LLVMBuildLoad2(builder_, int8ptr_t_, it->second, (rhsVar->name + ".load").c_str());
                            }
                        }
                    }
                }
            }
            
            // Declare strcmp function
            static LLVMValueRef strcmpFn = nullptr;
            if (!strcmpFn)
            {
                strcmpFn = LLVMGetNamedFunction(module_, "strcmp");
                if (!strcmpFn) {
                    LLVMTypeRef args[] = {int8ptr_t_, int8ptr_t_};
                    LLVMTypeRef ftype = LLVMFunctionType(int32_t_, args, 2, 0);
                                        strcmpFn = LLVMGetNamedFunction(module_, "strcmp");
                    if (!strcmpFn) strcmpFn = LLVMAddFunction(module_, "strcmp", ftype);
                }
            }
            
            LLVMValueRef args[] = {lhs, rhs};
            LLVMValueRef result = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strcmpFn), strcmpFn, args, 2, "strcmptmp");
            LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
            
            if (b->op == '=') // equals
                return LLVMBuildICmp(builder_, LLVMIntEQ, result, zero, "eqtmp");
            else // not equals
                return LLVMBuildICmp(builder_, LLVMIntNE, result, zero, "netmp");
        }
        
    if (b->op == '+')
        {
                                    TypeInfo lhsType, rhsType;
            try {
                lhsType = inferType(b->lhs.get());
                rhsType = inferType(b->rhs.get());
            } catch (...) {
                lhsType = TypeInfo(QuarkType::Unknown, {});
                rhsType = TypeInfo(QuarkType::Unknown, {});
            }
            
            if (IntegerTypeUtils::isNumericType(lhsType.type) && IntegerTypeUtils::isNumericType(rhsType.type)) {
                                if (IntegerTypeUtils::isFloatingType(lhsType.type) || IntegerTypeUtils::isFloatingType(rhsType.type)) {
                    // Float addition
                    LLVMValueRef lhs = genExpr(b->lhs.get());
                    LLVMValueRef rhs = genExpr(b->rhs.get());
                    
                                        if (lhsType.type == QuarkType::Float || rhsType.type == QuarkType::Float) {
                        // Convert both to float
                        if (lhsType.type == QuarkType::Int) {
                            lhs = LLVMBuildSIToFP(builder_, lhs, float_t_, "lhs_to_float");
                        } else if (lhsType.type == QuarkType::Double) {
                            lhs = LLVMBuildFPTrunc(builder_, lhs, float_t_, "lhs_to_float");
                        }
                        if (rhsType.type == QuarkType::Int) {
                            rhs = LLVMBuildSIToFP(builder_, rhs, float_t_, "rhs_to_float");
                        } else if (rhsType.type == QuarkType::Double) {
                            rhs = LLVMBuildFPTrunc(builder_, rhs, float_t_, "rhs_to_float");
                        }
                        return LLVMBuildFAdd(builder_, lhs, rhs, "addtmp");
                    } else {
                        // Convert both to double
                        if (lhsType.type == QuarkType::Int) {
                            lhs = LLVMBuildSIToFP(builder_, lhs, double_t_, "lhs_to_double");
                        } else if (lhsType.type == QuarkType::Float) {
                            lhs = LLVMBuildFPExt(builder_, lhs, double_t_, "lhs_to_double");
                        }
                        if (rhsType.type == QuarkType::Int) {
                            rhs = LLVMBuildSIToFP(builder_, rhs, double_t_, "rhs_to_double");
                        } else if (rhsType.type == QuarkType::Float) {
                            rhs = LLVMBuildFPExt(builder_, rhs, double_t_, "rhs_to_double");
                        }
                        return LLVMBuildFAdd(builder_, lhs, rhs, "addtmp");
                    }
                } else {
                    // Integer addition
                    LLVMValueRef lhs = genExprInt(b->lhs.get());
                    LLVMValueRef rhs = genExprInt(b->rhs.get());
                    return LLVMBuildAdd(builder_, lhs, rhs, "addtmp");
                }
            }
            
                        LLVMValueRef lhs = genExpr(b->lhs.get());
            LLVMValueRef rhs = genExpr(b->rhs.get());

                        if (builtinFunctions_ && builtinFunctions_->isBuiltin("str_concat")) {
                std::vector<LLVMValueRef> args = {lhs, rhs};
                LLVMValueRef result = builtinFunctions_->generateBuiltinCall("str_concat", args);
                if (result) {
                    return result;
                }
            }

                        if (g_function_map_) {
                auto it = g_function_map_->find("str_concat");
                if (it != g_function_map_->end()) {
                                        LLVMValueRef strConcatFn = it->second;
                    LLVMValueRef args[] = {lhs, rhs};
                    LLVMTypeRef fnType = LLVMGlobalGetValueType(strConcatFn);
                    return LLVMBuildCall2(builder_, fnType, strConcatFn, args, 2, "str_concat_call");
                }
            }

                        throw std::runtime_error("str_concat function not found - built-in or standard library version required");
        }
        
                TypeInfo lhsType, rhsType;
        try {
            lhsType = inferType(b->lhs.get());
            rhsType = inferType(b->rhs.get());
        } catch (...) {
            lhsType = TypeInfo(QuarkType::Unknown, {});
            rhsType = TypeInfo(QuarkType::Unknown, {});
        }
        
        if (IntegerTypeUtils::isFloatingType(lhsType.type) || IntegerTypeUtils::isFloatingType(rhsType.type)) {
            // Floating-point arithmetic
            LLVMValueRef lhs = genExpr(b->lhs.get());
            LLVMValueRef rhs = genExpr(b->rhs.get());
            
                        LLVMTypeRef targetType;
            if (lhsType.type == QuarkType::Double || rhsType.type == QuarkType::Double) {
                targetType = double_t_;
                // Convert operands to double
                if (lhsType.type == QuarkType::Int) {
                    lhs = LLVMBuildSIToFP(builder_, lhs, double_t_, "lhs_to_double");
                } else if (lhsType.type == QuarkType::Float) {
                    lhs = LLVMBuildFPExt(builder_, lhs, double_t_, "lhs_to_double");
                }
                if (rhsType.type == QuarkType::Int) {
                    rhs = LLVMBuildSIToFP(builder_, rhs, double_t_, "rhs_to_double");
                } else if (rhsType.type == QuarkType::Float) {
                    rhs = LLVMBuildFPExt(builder_, rhs, double_t_, "rhs_to_double");
                }
            } else {
                targetType = float_t_;
                // Convert operands to float
                if (lhsType.type == QuarkType::Int) {
                    lhs = LLVMBuildSIToFP(builder_, lhs, float_t_, "lhs_to_float");
                }
                if (rhsType.type == QuarkType::Int) {
                    rhs = LLVMBuildSIToFP(builder_, rhs, float_t_, "rhs_to_float");
                }
            }
            
            switch (b->op) {
            case '-': return LLVMBuildFSub(builder_, lhs, rhs, "subtmp");
            case '*': return LLVMBuildFMul(builder_, lhs, rhs, "multmp");
            case '/': return LLVMBuildFDiv(builder_, lhs, rhs, "divtmp");
            case '%': {
                                LLVMValueRef fmodFn;
                if (targetType == float_t_) {
                    // Use fmodf for float
                    fmodFn = LLVMGetNamedFunction(module_, "fmodf");
                    if (!fmodFn) {
                        LLVMTypeRef args[] = {float_t_, float_t_};
                        LLVMTypeRef ftype = LLVMFunctionType(float_t_, args, 2, 0);
                        fmodFn = LLVMAddFunction(module_, "fmodf", ftype);
                    }
                } else {
                    // Use fmod for double
                    fmodFn = LLVMGetNamedFunction(module_, "fmod");
                    if (!fmodFn) {
                        LLVMTypeRef args[] = {double_t_, double_t_};
                        LLVMTypeRef ftype = LLVMFunctionType(double_t_, args, 2, 0);
                        fmodFn = LLVMAddFunction(module_, "fmod", ftype);
                    }
                }
                LLVMValueRef args[] = {lhs, rhs};
                return LLVMBuildCall2(builder_, LLVMGlobalGetValueType(fmodFn), fmodFn, args, 2, "modtmp");
            }
            default:
                throw std::runtime_error("unsupported binary op");
            }
        } else {
            // Integer arithmetic
            LLVMValueRef lhs = genExprInt(b->lhs.get());
            LLVMValueRef rhs = genExprInt(b->rhs.get());
            switch (b->op)
            {
            case '-': return LLVMBuildSub(builder_, lhs, rhs, "subtmp");
            case '*': return LLVMBuildMul(builder_, lhs, rhs, "multmp");
            case '/': return LLVMBuildSDiv(builder_, lhs, rhs, "divtmp");
            case '%': return LLVMBuildSRem(builder_, lhs, rhs, "modtmp");
            default:
                throw std::runtime_error("unsupported binary op");
            }
        }
    }
    if (auto *v = dynamic_cast<VariableExprAST *>(expr))
    {
                auto fieldIt = struct_fields_.find(v->name);
        if (fieldIt != struct_fields_.end()) {
                        const std::string& structName = fieldIt->second.first;
            const std::string& fieldType = fieldIt->second.second;
            
            if (verbose_) {
                printf("[expression_codegen] resolving field '%s' to this.%s\n", v->name.c_str(), v->name.c_str());
            }
            
            // Get the "this" parameter
            auto thisIt = g_named_values_->find("this");
            if (thisIt == g_named_values_->end()) {
                throw std::runtime_error("struct field '" + v->name + "' accessed outside method context");
            }
            
                        auto structTypeIt = g_struct_types_->find(structName);
            if (structTypeIt == g_struct_types_->end()) {
                throw std::runtime_error("struct type not found: " + structName);
            }
            
            auto structDefIt = g_struct_defs_->find(structName);
            if (structDefIt == g_struct_defs_->end()) {
                throw std::runtime_error("struct definition not found: " + structName);
            }
            
            StructDefStmt* structDef = structDefIt->second;
            int fieldIndex = -1;
            for (size_t i = 0; i < structDef->fields.size(); ++i) {
                if (structDef->fields[i].first == v->name) {
                    fieldIndex = static_cast<int>(i);
                    break;
                }
            }
            
            if (fieldIndex == -1) {
                throw std::runtime_error("field not found in struct definition: " + v->name);
            }
            
                        LLVMValueRef thisPtr = LLVMBuildLoad2(builder_, LLVMPointerType(structTypeIt->second, 0), thisIt->second, "this_ptr.load");
            LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structTypeIt->second, thisPtr, fieldIndex, (v->name + ".ptr").c_str());
            
                        if (fieldType == "int") {
                return LLVMBuildLoad2(builder_, int32_t_, fieldPtr, (v->name + ".load").c_str());
            } else if (fieldType == "float") {
                return LLVMBuildLoad2(builder_, float_t_, fieldPtr, (v->name + ".load").c_str());
            } else if (fieldType == "double") {
                return LLVMBuildLoad2(builder_, double_t_, fieldPtr, (v->name + ".load").c_str());
            } else if (fieldType == "str") {
                return LLVMBuildLoad2(builder_, int8ptr_t_, fieldPtr, (v->name + ".load").c_str());
            } else if (fieldType == "bool") {
                return LLVMBuildLoad2(builder_, bool_t_, fieldPtr, (v->name + ".load").c_str());
            } else {
                // Handle struct types
                auto fieldStructTypeIt = g_struct_types_->find(fieldType);
                if (fieldStructTypeIt != g_struct_types_->end()) {
                    return LLVMBuildLoad2(builder_, fieldStructTypeIt->second, fieldPtr, (v->name + ".load").c_str());
                }
                throw std::runtime_error("unsupported field type: " + fieldType);
            }
        }
        
        if (g_named_values_) {
            auto it = g_named_values_->find(v->name);
            if (it != g_named_values_->end())
            {
                                auto pit = function_params_.find(v->name);
                bool isDirectParam = (pit != function_params_.end() && pit->second);
                
                if (isDirectParam) {
                    return it->second; // Use parameter directly
                } else if (g_named_types_) {
                                        auto typeIt = g_named_types_->find(v->name);
                    if (typeIt != g_named_types_->end()) {
                        if (typeIt->second == int8ptr_t_) {
                                                        return LLVMBuildLoad2(builder_, int8ptr_t_, it->second, (v->name + ".load").c_str());
                        } else if (typeIt->second == int32_t_) {
                                                        return LLVMBuildLoad2(builder_, int32_t_, it->second, (v->name + ".load").c_str());
                        } else if (typeIt->second == float_t_) {
                                                        return LLVMBuildLoad2(builder_, float_t_, it->second, (v->name + ".load").c_str());
                        } else if (typeIt->second == double_t_) {
                                                        return LLVMBuildLoad2(builder_, double_t_, it->second, (v->name + ".load").c_str());
                        } else if (typeIt->second == bool_t_) {
                                                        return LLVMBuildLoad2(builder_, bool_t_, it->second, (v->name + ".load").c_str());
                        } else if (LLVMGetTypeKind(typeIt->second) == LLVMPointerTypeKind) {
                                                        return LLVMBuildLoad2(builder_, typeIt->second, it->second, (v->name + ".load").c_str());
                        } else if (g_struct_types_) {
                                                        for (const auto& structPair : *g_struct_types_) {
                                if (typeIt->second == structPair.second) {
                                                                        return LLVMBuildLoad2(builder_, structPair.second, it->second, (v->name + ".load").c_str());
                                }
                            }
                        }
                    }
                }
                return it->second;             }
        }
        throw std::runtime_error("unknown variable '" + v->name + "'");
    }
    if (auto *c = dynamic_cast<CallExprAST *>(expr))
    {
                if (verbose_) {
            printf("[expression_codegen] checking function call '%s'\n", c->callee.c_str());
            printf("[expression_codegen] builtinFunctions_ = %p\n", builtinFunctions_);
        }
        
        if (builtinFunctions_ && builtinFunctions_->isBuiltin(c->callee)) {
            // Generate arguments
            std::vector<LLVMValueRef> args;
            for (const auto& arg : c->args) {
                args.push_back(genExpr(arg.get()));
            }
            
                        const BuiltinFunction* builtin = builtinFunctions_->getBuiltin(c->callee);
            bool isVoidFunction = (builtin && LLVMGetTypeKind(builtin->returnType) == LLVMVoidTypeKind);
            
                        LLVMValueRef result = builtinFunctions_->generateBuiltinCall(c->callee, args);
            if (result || isVoidFunction) {
                return result;             }
                        if (verbose_) {
                printf("[expression_codegen] '%s' is NOT a builtin function\n", c->callee.c_str());
            }
        }
        
                if (g_function_map_) {
            auto it = g_function_map_->find(c->callee);
            if (it == g_function_map_->end())
            {
                std::string availableFuncs = "";
                for (const auto& pair : *g_function_map_) {
                    if (!availableFuncs.empty()) availableFuncs += ", ";
                    availableFuncs += pair.first;
                }
                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                    auto file = sm->getFile(c->location.filename);
                    if (file) {
                        throw EnhancedCodeGenError("unknown function '" + c->callee + "'", c->location, file->content, ErrorCodes::FUNCTION_NOT_FOUND, (int)std::max<size_t>(1, c->callee.size()));
                    }
                }
                throw CodeGenError("unknown function '" + c->callee + "'. Available functions: [" + availableFuncs + "]", c->location);
            }

                        if (g_function_param_types_) {
                auto paramTypesIt = g_function_param_types_->find(c->callee);
                if (paramTypesIt != g_function_param_types_->end()) {
                    const auto& expectedTypes = paramTypesIt->second;
                    
                                        bool isVariadic = (g_variadic_functions_ && 
                                     g_variadic_functions_->find(c->callee) != g_variadic_functions_->end() &&
                                     (*g_variadic_functions_)[c->callee]);
                    
                    if (verbose_) {
                        printf("[codegen] function call '%s': isVariadic=%d, expectedTypes.size()=%zu, args.size()=%zu\n", 
                               c->callee.c_str(), isVariadic ? 1 : 0, expectedTypes.size(), c->args.size());
                        if (g_variadic_functions_) {
                            auto vit = g_variadic_functions_->find(c->callee);
                            if (vit != g_variadic_functions_->end()) {
                                printf("[codegen] found '%s' in g_variadic_functions_ with value %d\n", 
                                       c->callee.c_str(), vit->second ? 1 : 0);
                            } else {
                                printf("[codegen] '%s' not found in g_variadic_functions_\n", c->callee.c_str());
                            }
                        } else {
                            printf("[codegen] g_variadic_functions_ is null\n");
                        }
                    }
                    
                    if (isVariadic) {
                                                if (c->args.size() < expectedTypes.size()) {
                            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                                auto file = sm->getFile(c->location.filename);
                                if (file) {
                                    throw EnhancedCodeGenError("variadic function '" + c->callee + "' expects at least " +
                                                               std::to_string(expectedTypes.size()) + " arguments but got " + 
                                                               std::to_string(c->args.size()) + " arguments", c->location, file->content, ErrorCodes::INVALID_SYNTAX, (int)std::max<size_t>(1, c->callee.size()));
                                }
                            }
                            throw CodeGenError("variadic function '" + c->callee + "' expects at least " + 
                                             std::to_string(expectedTypes.size()) + " arguments but got " + 
                                             std::to_string(c->args.size()) + " arguments", c->location);
                        }
                    } else {
                                                if (c->args.size() != expectedTypes.size()) {
                            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                                auto file = sm->getFile(c->location.filename);
                                if (file) {
                                    throw EnhancedCodeGenError("function '" + c->callee + "' expects " +
                                                               std::to_string(expectedTypes.size()) + " arguments but got " + 
                                                               std::to_string(c->args.size()) + " arguments", c->location, file->content, ErrorCodes::INVALID_SYNTAX, (int)std::max<size_t>(1, c->callee.size()));
                                }
                            }
                            throw CodeGenError("function '" + c->callee + "' expects " + 
                                             std::to_string(expectedTypes.size()) + " arguments but got " + 
                                             std::to_string(c->args.size()) + " arguments", c->location);
                        }
                    }
                    
                                        size_t argsToCheck = isVariadic ? expectedTypes.size() : c->args.size();
                    for (size_t i = 0; i < argsToCheck; ++i) {
                        TypeInfo argType = inferType(c->args[i].get());
                                                QuarkType expectedQuarkType = llvmTypeToQuarkType(expectedTypes[i]);

                                                if (IntegerTypeUtils::isIntegerType(expectedQuarkType) &&
                            IntegerTypeUtils::isIntegerType(argType.type)) {
                            continue;
                        }

                                                if ((expectedQuarkType == QuarkType::Int || expectedQuarkType == QuarkType::Unknown) &&
                            IntegerTypeUtils::isIntegerType(argType.type)) {
                            continue;
                        }

                                                                                                if (expectedQuarkType == QuarkType::Float || expectedQuarkType == QuarkType::Double) {
                            if (IntegerTypeUtils::isIntegerType(argType.type) ||
                                argType.type == QuarkType::Float ||
                                argType.type == QuarkType::Double ||
                                argType.type == QuarkType::Unknown) {
                                continue;
                            }
                        }

                                                if (expectedQuarkType != QuarkType::Unknown) {
                                                        if (argType.type == QuarkType::Unknown) continue;
                                                        if (expectedQuarkType == QuarkType::String && argType.type == QuarkType::Array) continue;
                                                        if (expectedQuarkType == QuarkType::String && argType.type == QuarkType::Pointer) continue;
                                                        if (expectedQuarkType == QuarkType::Array && argType.type == QuarkType::Pointer) continue;
                            if (argType.type != expectedQuarkType) {
                            auto typeToStr = [](QuarkType t) {
                                if (IntegerTypeUtils::isIntegerType(t)) return std::string("integer");
                                switch (t) {
                                    case QuarkType::String: return std::string("string");
                                    case QuarkType::Boolean: return std::string("boolean");
                                    case QuarkType::Void: return std::string("void");
                                    case QuarkType::Array: return std::string("array");
                                    default: return std::string("unknown");
                                }
                            };
                            std::string expectedStr = typeToStr(expectedQuarkType);
                            std::string actualStr = typeToStr(argType.type);
                            throw CodeGenError(
                                "Type mismatch in function call to '" + c->callee +
                                "' at argument " + std::to_string(i + 1) +
                                ": expected " + expectedStr + " but got " + actualStr,
                                c->args[i]->location);
                            }
                        }
                        
                    }
                }
            }

            LLVMValueRef fn = it->second;
            std::vector<LLVMValueRef> args;
            if (verbose_)
                printf("[codegen] genExpr: generating function call to %s with %zu args\n", c->callee.c_str(), c->args.size());
            
                        if (g_function_param_types_) {
                auto paramTypesIt = g_function_param_types_->find(c->callee);
                if (paramTypesIt != g_function_param_types_->end()) {
                    const auto& expectedTypes = paramTypesIt->second;
                    
                                        for (size_t i = 0; i < c->args.size(); ++i)
                    {
                        if (verbose_)
                            printf("[codegen] genExpr: generating argument %zu with expected type\n", i);
                        LLVMValueRef argVal;
                        if (i < expectedTypes.size()) {
                                                        LLVMTypeRef expTy = expectedTypes[i];
                            if (isLLVMIntegerType(expTy)) {
                                argVal = genExprIntWithType(c->args[i].get(), expTy);
                            } else if (expTy == bool_t_) {
                                argVal = genExprBool(c->args[i].get());
                            } else if (expTy == float_t_) {
                                // Coerce to float
                                TypeInfo at = inferType(c->args[i].get());
                                if (at.type == QuarkType::Float) {
                                    argVal = genExpr(c->args[i].get());
                                } else if (at.type == QuarkType::Double) {
                                    LLVMValueRef dv = genExpr(c->args[i].get());
                                    argVal = LLVMBuildFPTrunc(builder_, dv, float_t_, "to_float");
                                } else if (IntegerTypeUtils::isIntegerType(at.type)) {
                                    LLVMValueRef iv = genExprInt(c->args[i].get());
                                    argVal = LLVMBuildSIToFP(builder_, iv, float_t_, "i_to_float");
                                } else {
                                    argVal = genExpr(c->args[i].get());
                                }
                            } else if (expTy == double_t_) {
                                // Coerce to double
                                TypeInfo at = inferType(c->args[i].get());
                                if (at.type == QuarkType::Double) {
                                    argVal = genExpr(c->args[i].get());
                                } else if (at.type == QuarkType::Float) {
                                    LLVMValueRef fv = genExpr(c->args[i].get());
                                    argVal = LLVMBuildFPExt(builder_, fv, double_t_, "to_double");
                                } else if (IntegerTypeUtils::isIntegerType(at.type)) {
                                    LLVMValueRef iv = genExprInt(c->args[i].get());
                                    argVal = LLVMBuildSIToFP(builder_, iv, double_t_, "i_to_double");
                                } else {
                                    argVal = genExpr(c->args[i].get());
                                }
                            } else if (LLVMGetTypeKind(expTy) == LLVMPointerTypeKind) {
                                                                LLVMValueRef v = genExpr(c->args[i].get());
                                LLVMTypeRef vTy = LLVMTypeOf(v);
                                if (vTy != expTy) {
                                                                                                            if (LLVMGetTypeKind(vTy) == LLVMFunctionTypeKind) {
                                                                                LLVMTypeRef fnPtrTy = LLVMPointerType(vTy, 0);
                                        v = LLVMConstBitCast(v, fnPtrTy);
                                    }
                                    v = LLVMBuildBitCast(builder_, v, expTy, "to_ptr_arg");
                                }
                                argVal = v;
                            } else {
                                // String or other types
                                argVal = genExpr(c->args[i].get());
                            }
                        } else {
                                                        TypeInfo argType = inferType(c->args[i].get());
                            if (argType.type == QuarkType::Int) {
                                argVal = genExprInt(c->args[i].get());
                            } else if (argType.type == QuarkType::Boolean) {
                                argVal = genExprBool(c->args[i].get());
                            } else {
                                // String or other types
                                argVal = genExpr(c->args[i].get());
                            }
                        }
                        args.push_back(argVal);
                        if (verbose_)
                            printf("[codegen] genExpr: generated argument %zu with expected type\n", i);
                    }
                } else {
                                        for (size_t i = 0; i < c->args.size(); i++)
                    {
                        if (verbose_)
                            printf("[codegen] genExpr: generating argument %zu\n", i);
                        args.push_back(genExpr(c->args[i].get()));
                        if (verbose_)
                            printf("[codegen] genExpr: generated argument %zu\n", i);
                    }
                }
            } else {
                                for (size_t i = 0; i < c->args.size(); i++)
                {
                    if (verbose_)
                        printf("[codegen] genExpr: generating argument %zu\n", i);
                    args.push_back(genExpr(c->args[i].get()));
                    if (verbose_)
                        printf("[codegen] genExpr: generated argument %zu\n", i);
                }
            }
            if (verbose_)
                printf("[codegen] genExpr: calling function %s\n", c->callee.c_str());
            LLVMTypeRef fnType = LLVMGlobalGetValueType(fn);
            if (verbose_)
                printf("[codegen] genExpr: got function type\n");
            
                        LLVMTypeRef returnType = LLVMGetReturnType(fnType);
            bool isVoid = (LLVMGetTypeKind(returnType) == LLVMVoidTypeKind);
            
            LLVMValueRef result = LLVMBuildCall2(builder_, fnType, fn, args.data(), static_cast<unsigned int>(args.size()), isVoid ? "" : "call");
            if (verbose_)
                printf("[codegen] genExpr: built function call\n");
            return result;
        }
    }

    throw std::runtime_error("unsupported expr in codegen");
}

LLVMValueRef ExpressionCodeGen::genExprInt(ExprAST *expr)
{
    if (!expr)
        throw std::runtime_error("null expr in genExprInt");

        if (auto *cast = dynamic_cast<CastExpr *>(expr)) {
                if (IntegerTypeUtils::stringToQuarkType(cast->targetTypeName) == QuarkType::Int) {
            LLVMValueRef v = genExpr(expr);
            return (LLVMTypeOf(v) == int32_t_) ? v : LLVMBuildZExt(builder_, v, int32_t_, "to_i32_ctx");
        }
                TypeInfo srcT;
        try { srcT = inferType(cast->operand.get()); } catch (...) { srcT = TypeInfo(QuarkType::Unknown, expr->location); }
        LLVMValueRef v = genExpr(cast->operand.get());
        if (IntegerTypeUtils::isIntegerType(srcT.type)) {
            return (LLVMTypeOf(v) == int32_t_) ? v : LLVMBuildZExt(builder_, v, int32_t_, "to_i32_any");
        }
        if (srcT.type == QuarkType::Float || srcT.type == QuarkType::Double) {
            return LLVMBuildFPToSI(builder_, v, int32_t_, "fp_to_i32_ctx");
        }
        if (srcT.type == QuarkType::Boolean) {
            return LLVMBuildZExt(builder_, v, int32_t_, "bool_to_i32_ctx");
        }
        throw std::runtime_error("unsupported cast in integer context");
    }

    if (auto *n = dynamic_cast<NumberExprAST *>(expr))
    {
        return LLVMConstInt(LLVMInt32TypeInContext(ctx_), static_cast<long long>(n->value), 0);
    }

    if (auto *v = dynamic_cast<VariableExprAST *>(expr))
    {
                auto fieldIt = struct_fields_.find(v->name);
        if (fieldIt != struct_fields_.end()) {
                        const std::string& structName = fieldIt->second.first;
            const std::string& fieldType = fieldIt->second.second;
            
            if (verbose_) {
                printf("[expression_codegen] resolving field '%s' to this.%s in integer context\n", 
                       v->name.c_str(), v->name.c_str());
            }
            
                        if (fieldType != "int") {
                throw std::runtime_error("field '" + v->name + "' is not an integer type in integer context");
            }
            
            // Get the "this" parameter
            auto thisIt = g_named_values_->find("this");
            if (thisIt == g_named_values_->end()) {
                throw std::runtime_error("struct field '" + v->name + "' accessed outside method context");
            }
            
                        auto structTypeIt = g_struct_types_->find(structName);
            if (structTypeIt == g_struct_types_->end()) {
                throw std::runtime_error("struct type not found: " + structName);
            }
            
            auto structDefIt = g_struct_defs_->find(structName);
            if (structDefIt == g_struct_defs_->end()) {
                throw std::runtime_error("struct definition not found: " + structName);
            }
            
            StructDefStmt* structDef = structDefIt->second;
            int fieldIndex = -1;
            for (size_t i = 0; i < structDef->fields.size(); ++i) {
                if (structDef->fields[i].first == v->name) {
                    fieldIndex = static_cast<int>(i);
                    break;
                }
            }
            
            if (fieldIndex == -1) {
                throw std::runtime_error("field not found in struct definition: " + v->name);
            }
            
                        LLVMValueRef thisPtr = LLVMBuildLoad2(builder_, LLVMPointerType(structTypeIt->second, 0), thisIt->second, "this_ptr.load");
            LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structTypeIt->second, thisPtr, fieldIndex, (v->name + ".ptr").c_str());
            
            // Load the integer field value
            return LLVMBuildLoad2(builder_, int32_t_, fieldPtr, (v->name + ".load").c_str());
        }
        
        if (!g_named_values_)
            throw std::runtime_error("global symbol tables not initialized");
            
        auto it = g_named_values_->find(v->name);
        if (it == g_named_values_->end())
            throw std::runtime_error(std::string("unknown variable '") + v->name + "' in integer context");

        LLVMValueRef val = it->second;
        LLVMTypeRef valType = LLVMTypeOf(val);

                auto tit = g_named_types_->find(v->name);
        if (tit != g_named_types_->end())
        {
            LLVMTypeRef namedTy = tit->second;
            if (LLVMGetTypeKind(namedTy) == LLVMIntegerTypeKind)
            {
                unsigned w = LLVMGetIntTypeWidth(namedTy);
                                auto pit = function_params_.find(v->name);
                bool isDirectParam = (pit != function_params_.end() && pit->second);
                LLVMValueRef loaded;
                if (!isDirectParam && LLVMGetTypeKind(valType) == LLVMPointerTypeKind) {
                    loaded = LLVMBuildLoad2(builder_, namedTy, val, (v->name + ".load").c_str());
                } else {
                    loaded = val;
                }
                // Normalize to i32
                if (w == 32) {
                    return (LLVMGetTypeKind(LLVMTypeOf(loaded)) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(LLVMTypeOf(loaded)) == 32)
                        ? loaded
                        : LLVMBuildZExt(builder_, loaded, LLVMInt32TypeInContext(ctx_), "to_i32");
                } else if (w > 32) {
                    return LLVMBuildTrunc(builder_, loaded, LLVMInt32TypeInContext(ctx_), "trunc_to_i32");
                } else { // w < 32
                                        QuarkType q = getVariableType(v->name);
                    if (isSignedIntegerType(q)) {
                        return LLVMBuildSExt(builder_, loaded, LLVMInt32TypeInContext(ctx_), "sext_to_i32");
                    } else {
                        return LLVMBuildZExt(builder_, loaded, LLVMInt32TypeInContext(ctx_), "zext_to_i32");
                    }
                }
            }
            // Not an integer
            throw std::runtime_error("variable is not integer in integer context");
        }

                if (LLVMGetTypeKind(valType) == LLVMIntegerTypeKind)
        {
            unsigned w = LLVMGetIntTypeWidth(valType);
            if (w == 32) return val;
            if (w > 32) return LLVMBuildTrunc(builder_, val, LLVMInt32TypeInContext(ctx_), "trunc_to_i32");
                        return LLVMBuildSExt(builder_, val, LLVMInt32TypeInContext(ctx_), "sext_to_i32");
        }
        if (LLVMGetTypeKind(valType) == LLVMPointerTypeKind)
        {
                        return LLVMBuildLoad2(builder_, LLVMInt32TypeInContext(ctx_), val, (v->name + ".load").c_str());
        }

        throw std::runtime_error("variable is not integer in integer context");
    }

        if (auto *deref = dynamic_cast<DereferenceExpr *>(expr)) {
        if (verbose_) printf("[codegen] generating dereference expression in integer context\n");
        
                LLVMValueRef ptrValue = genExpr(deref->operand.get());

        // Ensure pointer type
        if (LLVMGetTypeKind(LLVMTypeOf(ptrValue)) != LLVMPointerTypeKind) {
            auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                auto file = sm->getFile(expr->location.filename);
                if (file) {
                    throw EnhancedCodeGenError("dereference of non-pointer value in integer context", expr->location, file->content, ErrorCodes::INVALID_OPERATION, 1);
                }
            }
            throw CodeGenError("dereference of non-pointer value in integer context", expr->location);
        }
                LLVMValueRef result = LLVMBuildLoad2(builder_, int32_t_, ptrValue, "deref_int");
        if (verbose_) printf("[codegen] dereferenced pointer for integer\n");
        return result;
    }

    if (auto *u = dynamic_cast<UnaryExprAST *>(expr))
    {
        LLVMValueRef operand = genExprInt(u->operand.get());
        
        if (u->op == '-')
        {
            return LLVMBuildNeg(builder_, operand, "neg");
        }
        else if (u->op == '!')
        {
                        LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0);
            LLVMValueRef cmp = LLVMBuildICmp(builder_, LLVMIntEQ, operand, zero, "iszero");
            // Convert back to i32 (0 or 1)
            return LLVMBuildZExt(builder_, cmp, LLVMInt32TypeInContext(ctx_), "boolnot");
        }
        
        throw std::runtime_error("unsupported unary operator in integer context");
    }

    if (auto *b = dynamic_cast<BinaryExprAST *>(expr))
    {
        LLVMValueRef L = genExprInt(b->lhs.get());
        LLVMValueRef R = genExprInt(b->rhs.get());
        switch (b->op)
        {
        case '+': return LLVMBuildAdd(builder_, L, R, "addtmp");
        case '-': return LLVMBuildSub(builder_, L, R, "subtmp");
        case '*': return LLVMBuildMul(builder_, L, R, "multmp");
        case '/': return LLVMBuildSDiv(builder_, L, R, "divtmp");
        case '%': return LLVMBuildSRem(builder_, L, R, "modtmp");
        default:
            throw std::runtime_error("unsupported binary op in integer expression");
        }
    }

        if (auto *c = dynamic_cast<CallExprAST *>(expr))
    {
                
                if (!g_function_map_)
            throw std::runtime_error("global symbol tables not initialized");
            
        auto it = g_function_map_->find(c->callee);
        if (it == g_function_map_->end())
            throw std::runtime_error("unknown function '" + c->callee + "' in integer context");
        
                auto typeIt = functionTypes_.find(c->callee);
        if (typeIt != functionTypes_.end() && isIntegerQuarkType(typeIt->second.type))
        {
                        std::vector<LLVMValueRef> args;
            if (g_function_param_types_) {
                auto paramTypesIt = g_function_param_types_->find(c->callee);
                if (paramTypesIt != g_function_param_types_->end()) {
                    const auto& expectedTypes = paramTypesIt->second;
                    
                                        bool isVariadic = (g_variadic_functions_ && 
                                     g_variadic_functions_->find(c->callee) != g_variadic_functions_->end() &&
                                     (*g_variadic_functions_)[c->callee]);
                    
                    if (isVariadic) {
                                                if (c->args.size() < expectedTypes.size()) {
                            throw std::runtime_error("variadic function '" + c->callee + "' expects at least " + 
                                                   std::to_string(expectedTypes.size()) + " arguments but got " + 
                                                   std::to_string(c->args.size()) + " arguments");
                        }
                    } else {
                                                if (c->args.size() != expectedTypes.size()) {
                            throw std::runtime_error("function '" + c->callee + "' expects " + 
                                                   std::to_string(expectedTypes.size()) + " arguments but got " + 
                                                   std::to_string(c->args.size()) + " arguments");
                        }
                    }
                    
                                        size_t argsToCheck = isVariadic ? expectedTypes.size() : c->args.size();
                    for (size_t i = 0; i < argsToCheck; ++i) {
                        TypeInfo argType = inferType(c->args[i].get());
                        QuarkType expectedQuarkType = QuarkType::Unknown;
                        
                                                if (expectedTypes[i] == int32_t_) {
                            expectedQuarkType = QuarkType::Int;
                        } else if (expectedTypes[i] == int8ptr_t_) {
                            expectedQuarkType = QuarkType::String;
                        } else if (expectedTypes[i] == LLVMPointerType(int8ptr_t_, 0)) {
                            // char** (array of strings)
                            expectedQuarkType = QuarkType::Array;
                        } else if (expectedTypes[i] == LLVMPointerType(int32_t_, 0)) {
                            // int* (integer pointer)
                            expectedQuarkType = QuarkType::Pointer;
                        } else if (expectedTypes[i] == bool_t_) {
                            expectedQuarkType = QuarkType::Boolean;
                        }
                        
                                                if (expectedQuarkType != QuarkType::Unknown && 
                            argType.type != QuarkType::Unknown) {
                            bool ok = true;
                            if (IntegerTypeUtils::isIntegerType(expectedQuarkType)) {
                                ok = IntegerTypeUtils::isIntegerType(argType.type);
                            } else if (expectedQuarkType == QuarkType::String && argType.type == QuarkType::Array) {
                                                                ok = true;
                            } else if (expectedQuarkType == QuarkType::Array && argType.type == QuarkType::Pointer) {
                                                                ok = true;
                            } else if (expectedQuarkType == QuarkType::String && argType.type == QuarkType::Pointer) {
                                                                ok = true;
                            } else if (expectedQuarkType == QuarkType::Pointer && argType.type == QuarkType::Pointer) {
                                                                ok = true;
                            } else {
                                ok = (argType.type == expectedQuarkType);
                            }
                            if (!ok) {
                                auto typeToStr = [](QuarkType t){
                                    if (IntegerTypeUtils::isIntegerType(t)) return std::string("integer");
                                    switch (t) {
                                        case QuarkType::String: return std::string("string");
                                        case QuarkType::Boolean: return std::string("boolean");
                                        case QuarkType::Void: return std::string("void");
                                        case QuarkType::Array: return std::string("array");
                                        case QuarkType::Float: return std::string("float");
                                        case QuarkType::Double: return std::string("double");
                                        case QuarkType::Pointer: return std::string("pointer");
                                        default: return std::string("unknown");
                                    }
                                };
                                std::string expectedStr = typeToStr(expectedQuarkType);
                                std::string actualStr = typeToStr(argType.type);
                                throw std::runtime_error("Type mismatch in function call to '" + c->callee + 
                                                       "' at argument " + std::to_string(i + 1) + 
                                                       ": expected " + expectedStr + " but got " + actualStr);
                            }
                        }
                        
                                                if (expectedQuarkType != QuarkType::Unknown && argType.type == QuarkType::Unknown) {
                                                        continue;
                        }
                    }
                    
                                        for (size_t i = 0; i < c->args.size(); ++i)
                    {
                        LLVMValueRef argVal;
                        if (i < expectedTypes.size()) {
                                                        if (isLLVMIntegerType(expectedTypes[i])) {
                                argVal = genExprIntWithType(c->args[i].get(), expectedTypes[i]);
                            } else if (expectedTypes[i] == bool_t_) {
                                argVal = genExprBool(c->args[i].get());
                            } else {
                                // String or other types
                                argVal = genExpr(c->args[i].get());
                            }
                        } else {
                                                        argVal = genExpr(c->args[i].get());
                        }
                        args.push_back(argVal);
                    }
                } else {
                                        for (auto& arg : c->args) {
                        args.push_back(genExpr(arg.get()));
                    }
                }
            } else {
                                for (auto& arg : c->args) {
                    args.push_back(genExpr(arg.get()));
                }
            }
            
                                    if (builtinFunctions_ && builtinFunctions_->isBuiltin(c->callee)) {
                LLVMValueRef built = builtinFunctions_->generateBuiltinCall(c->callee, args);
                if (!built) {
                    throw std::runtime_error("builtin '" + c->callee + "' failed to generate");
                }

                                LLVMTypeRef retTy = LLVMTypeOf(built);
                if (LLVMGetTypeKind(retTy) == LLVMIntegerTypeKind) {
                    unsigned w = LLVMGetIntTypeWidth(retTy);
                    if (w == 32) {
                        // ok
                    } else if (w > 32) {
                        built = LLVMBuildTrunc(builder_, built, LLVMInt32TypeInContext(ctx_), "trunc_to_i32");
                    } else {
                        if (isSignedIntegerType(typeIt->second.type)) {
                            built = LLVMBuildSExt(builder_, built, LLVMInt32TypeInContext(ctx_), "sext_to_i32");
                        } else {
                            built = LLVMBuildZExt(builder_, built, LLVMInt32TypeInContext(ctx_), "zext_to_i32");
                        }
                    }
                }

                if (verbose_)
                    printf("[codegen] generated integer builtin call to %s\n", c->callee.c_str());

                return built;
            }

                        LLVMValueRef fn = it->second;
            LLVMTypeRef fnType = LLVMGlobalGetValueType(fn);

                        LLVMTypeRef returnType = LLVMGetReturnType(fnType);
            bool isVoid = (LLVMGetTypeKind(returnType) == LLVMVoidTypeKind);

            LLVMValueRef result = LLVMBuildCall2(builder_, fnType, fn,
                                               args.empty() ? nullptr : args.data(),
                                               static_cast<unsigned int>(args.size()), isVoid ? "" : "call_i32");

                        if (LLVMGetTypeKind(returnType) == LLVMIntegerTypeKind) {
                unsigned w = LLVMGetIntTypeWidth(returnType);
                if (w == 32) {
                    // ok
                } else if (w > 32) {
                    result = LLVMBuildTrunc(builder_, result, LLVMInt32TypeInContext(ctx_), "trunc_to_i32");
                } else /* w < 32 */ {
                                        if (isSignedIntegerType(typeIt->second.type)) {
                        result = LLVMBuildSExt(builder_, result, LLVMInt32TypeInContext(ctx_), "sext_to_i32");
                    } else {
                        result = LLVMBuildZExt(builder_, result, LLVMInt32TypeInContext(ctx_), "zext_to_i32");
                    }
                }
            }

            if (verbose_)
                printf("[codegen] generated integer function call to %s\n", c->callee.c_str());

            return result;
        }
        
        throw std::runtime_error("function '" + c->callee + "' does not return integer type");
    }
    
        if (auto *staticCall = dynamic_cast<StaticCallExpr *>(expr))
    {
        return genStaticCall(staticCall);
    }
    
        if (auto *methodCall = dynamic_cast<MethodCallExpr *>(expr))
    {
        LLVMValueRef result = genMethodCall(methodCall);
                return result;
    }
    
        if (auto *memberAccess = dynamic_cast<MemberAccessExpr *>(expr))
    {
        return genMemberAccess(memberAccess);
    }
    
        if (auto *arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr))
    {
                TypeInfo elementType = inferType(expr);
        if (elementType.type == QuarkType::Int || IntegerTypeUtils::isIntegerType(elementType.type)) {
            return genArrayAccess(arrayAccess);
        } else {
            throw std::runtime_error("array access does not return integer type");
        }
    }
    
    throw std::runtime_error("unsupported runtime integer expression");
}

LLVMValueRef ExpressionCodeGen::genExprBool(ExprAST *expr)
{
    if (!expr)
        throw std::runtime_error("null expr in genExprBool");

        if (auto *cast = dynamic_cast<CastExpr *>(expr)) {
                if (IntegerTypeUtils::stringToQuarkType(cast->targetTypeName) == QuarkType::Boolean) {
            return genExpr(expr);
        }
                TypeInfo srcT;
        try { srcT = inferType(cast->operand.get()); } catch (...) { srcT = TypeInfo(QuarkType::Unknown, expr->location); }
        LLVMValueRef v = genExpr(cast->operand.get());
        if (IntegerTypeUtils::isIntegerType(srcT.type)) {
            LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
            LLVMValueRef i32v = (LLVMTypeOf(v) == int32_t_) ? v : LLVMBuildZExt(builder_, v, int32_t_, "to_i32_bool");
            return LLVMBuildICmp(builder_, LLVMIntNE, i32v, zero, "int_to_bool_ctx");
        }
        if (srcT.type == QuarkType::Float) {
            LLVMValueRef zero = LLVMConstReal(float_t_, 0.0);
            return LLVMBuildFCmp(builder_, LLVMRealONE, v, zero, "float_to_bool_ctx");
        }
        if (srcT.type == QuarkType::Double) {
            LLVMValueRef zero = LLVMConstReal(double_t_, 0.0);
            return LLVMBuildFCmp(builder_, LLVMRealONE, v, zero, "double_to_bool_ctx");
        }
        if (srcT.type == QuarkType::Boolean) {
            return v;
        }
        throw std::runtime_error("unsupported cast in boolean context");
    }

    if (auto *b = dynamic_cast<BoolExprAST *>(expr))
    {
        return LLVMConstInt(bool_t_, b->value ? 1 : 0, 0);
    }

    if (auto *memberAccess = dynamic_cast<MemberAccessExpr *>(expr))
    {
        TypeInfo fieldInfo;
        try {
            fieldInfo = inferType(memberAccess);
        } catch (...) {
            fieldInfo = TypeInfo(QuarkType::Unknown, expr->location);
        }

        if (fieldInfo.type != QuarkType::Boolean) {
            throw std::runtime_error("member access is not boolean in boolean context");
        }

        LLVMValueRef fieldValue = genMemberAccess(memberAccess);
        LLVMTypeRef fieldType = LLVMTypeOf(fieldValue);

        if (fieldType == bool_t_) {
            return fieldValue;
        }

        if (LLVMGetTypeKind(fieldType) == LLVMIntegerTypeKind) {
            LLVMValueRef zero = LLVMConstInt(fieldType, 0, 0);
            return LLVMBuildICmp(builder_, LLVMIntNE, fieldValue, zero, "member_bool_cast");
        }

        if (LLVMGetTypeKind(fieldType) == LLVMPointerTypeKind) {
            LLVMTypeRef elementType = LLVMGetElementType(fieldType);
            if (elementType == bool_t_) {
                std::string loadName = memberAccess->fieldName + ".boolload";
                return LLVMBuildLoad2(builder_, bool_t_, fieldValue, loadName.c_str());
            }
        }

        throw std::runtime_error("member access did not yield boolean-compatible value");
    }

    if (auto *v = dynamic_cast<VariableExprAST *>(expr))
    {
                auto fieldIt = struct_fields_.find(v->name);
        if (fieldIt != struct_fields_.end()) {
                        const std::string& structName = fieldIt->second.first;
            const std::string& fieldType = fieldIt->second.second;
            
            if (verbose_) {
                printf("[expression_codegen] resolving field '%s' to this.%s in boolean context\n", 
                       v->name.c_str(), v->name.c_str());
            }
            
                        if (fieldType != "bool") {
                throw std::runtime_error("field '" + v->name + "' is not a boolean type in boolean context");
            }
            
            // Get the "this" parameter
            auto thisIt = g_named_values_->find("this");
            if (thisIt == g_named_values_->end()) {
                throw std::runtime_error("struct field '" + v->name + "' accessed outside method context");
            }
            
                        auto structTypeIt = g_struct_types_->find(structName);
            if (structTypeIt == g_struct_types_->end()) {
                throw std::runtime_error("struct type not found: " + structName);
            }
            
            auto structDefIt = g_struct_defs_->find(structName);
            if (structDefIt == g_struct_defs_->end()) {
                throw std::runtime_error("struct definition not found: " + structName);
            }
            
            StructDefStmt* structDef = structDefIt->second;
            int fieldIndex = -1;
            for (size_t i = 0; i < structDef->fields.size(); ++i) {
                if (structDef->fields[i].first == v->name) {
                    fieldIndex = static_cast<int>(i);
                    break;
                }
            }
            
            if (fieldIndex == -1) {
                throw std::runtime_error("field not found in struct definition: " + v->name);
            }
            
                        LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structTypeIt->second, thisIt->second, fieldIndex, (v->name + ".ptr").c_str());
            
            // Load the boolean field value
            return LLVMBuildLoad2(builder_, bool_t_, fieldPtr, (v->name + ".load").c_str());
        }
        
        if (!g_named_values_)
            throw std::runtime_error("global symbol tables not initialized");
            
        auto it = g_named_values_->find(v->name);
        if (it == g_named_values_->end())
            throw std::runtime_error(std::string("unknown variable '") + v->name + "' in boolean context");

        LLVMValueRef val = it->second;
        LLVMTypeRef valType = LLVMTypeOf(val);

                auto tit = g_named_types_->find(v->name);
        if (tit != g_named_types_->end())
        {
            if (tit->second == bool_t_)
            {
                if (LLVMGetTypeKind(valType) == LLVMPointerTypeKind)
                {
                    return LLVMBuildLoad2(builder_, bool_t_, val, (v->name + ".load").c_str());
                }
                if (LLVMGetTypeKind(valType) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(valType) == 1)
                {
                    return val;
                }
                throw std::runtime_error("variable is not bool in boolean context");
            }
            throw std::runtime_error("variable is not bool in boolean context");
        }

                if (LLVMGetTypeKind(valType) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(valType) == 1)
        {
            return val;
        }
        if (LLVMGetTypeKind(valType) == LLVMPointerTypeKind)
        {
                        return LLVMBuildLoad2(builder_, bool_t_, val, (v->name + ".load").c_str());
        }

        throw std::runtime_error("variable is not bool in boolean context");
    }

    if (auto *u = dynamic_cast<UnaryExprAST *>(expr))
    {
        if (u->op == '!')
        {
            LLVMValueRef operand = genExprBool(u->operand.get());
            return LLVMBuildNot(builder_, operand, "not");
        }
        else if (u->op == '-')
        {
                                    LLVMValueRef operand = genExprInt(u->operand.get());
            LLVMValueRef neg = LLVMBuildNeg(builder_, operand, "neg");
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0);
            return LLVMBuildICmp(builder_, LLVMIntNE, neg, zero, "negbool");
        }
        
        throw std::runtime_error("unsupported unary operator in boolean context");
    }

    if (auto *c = dynamic_cast<CallExprAST *>(expr))
    {
                        auto it = functionTypes_.find(c->callee);
        if (it != functionTypes_.end() && it->second.type == QuarkType::Boolean) {
                        std::vector<LLVMValueRef> args;
            
                        if (g_function_param_types_) {
                auto paramTypesIt = g_function_param_types_->find(c->callee);
                if (paramTypesIt != g_function_param_types_->end()) {
                    const auto& expectedTypes = paramTypesIt->second;
                    
                                        for (size_t i = 0; i < c->args.size(); ++i)
                    {
                        LLVMValueRef argVal;
                        if (i < expectedTypes.size()) {
                                                        if (isLLVMIntegerType(expectedTypes[i])) {
                                argVal = genExprIntWithType(c->args[i].get(), expectedTypes[i]);
                            } else if (expectedTypes[i] == bool_t_) {
                                argVal = genExprBool(c->args[i].get());
                            } else {
                                // String or other types
                                argVal = genExpr(c->args[i].get());
                            }
                        } else {
                                                        argVal = genExpr(c->args[i].get());
                        }
                        args.push_back(argVal);
                    }
                } else {
                                        for (size_t i = 0; i < c->args.size(); i++) {
                        args.push_back(genExpr(c->args[i].get()));
                    }
                }
            } else {
                                for (size_t i = 0; i < c->args.size(); i++) {
                    args.push_back(genExpr(c->args[i].get()));
                }
            }
            
                        LLVMValueRef func = LLVMGetNamedFunction(module_, c->callee.c_str());
            if (!func) {
                throw std::runtime_error("function not found: " + c->callee);
            }
            
            return LLVMBuildCall2(builder_, LLVMGlobalGetValueType(func), func, 
                                args.empty() ? nullptr : args.data(), static_cast<unsigned int>(args.size()), "calltmp");
        }
        throw std::runtime_error("function does not return bool: " + c->callee);
    }

        if (auto *m = dynamic_cast<MethodCallExpr *>(expr))
    {
                TypeInfo retInfo = inferType(m);
        if (retInfo.type == QuarkType::Boolean) {
                        LLVMValueRef v = genMethodCall(m);
                        if (LLVMTypeOf(v) == bool_t_) return v;
                        if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind) {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(v), 0, 0);
                return LLVMBuildICmp(builder_, LLVMIntNE, v, zero, "to_bool_meth");
            }
            throw std::runtime_error("method call did not yield boolean-compatible value");
        }
    }

        if (auto *staticCall = dynamic_cast<StaticCallExpr *>(expr))
    {
        return genStaticCall(staticCall);
    }

    if (auto *b = dynamic_cast<BinaryExprAST *>(expr))
    {
        if (b->op == '&') {
            LLVMValueRef lhs = genExprBool(b->lhs.get());
            LLVMBasicBlockRef lhsBlock = LLVMGetInsertBlock(builder_);
            LLVMValueRef parentFn = LLVMGetBasicBlockParent(lhsBlock);
            LLVMBasicBlockRef rhsBlock = LLVMAppendBasicBlockInContext(ctx_, parentFn, "land.rhs");
            LLVMBasicBlockRef endBlock = LLVMAppendBasicBlockInContext(ctx_, parentFn, "land.end");

            LLVMBuildCondBr(builder_, lhs, rhsBlock, endBlock);

            LLVMPositionBuilderAtEnd(builder_, rhsBlock);
            LLVMValueRef rhs = genExprBool(b->rhs.get());
            LLVMBuildBr(builder_, endBlock);
            LLVMBasicBlockRef rhsEvalBlock = LLVMGetInsertBlock(builder_);

            LLVMPositionBuilderAtEnd(builder_, endBlock);
            LLVMValueRef phi = LLVMBuildPhi(builder_, bool_t_, "land.result");

            LLVMValueRef incomingVals[2];
            LLVMBasicBlockRef incomingBlocks[2];
            incomingVals[0] = lhs;
            incomingBlocks[0] = lhsBlock;
            incomingVals[1] = rhs;
            incomingBlocks[1] = rhsEvalBlock;
            LLVMAddIncoming(phi, incomingVals, incomingBlocks, 2);

            return phi;
        }
        if (b->op == '|') {
            LLVMValueRef lhs = genExprBool(b->lhs.get());
            LLVMBasicBlockRef lhsBlock = LLVMGetInsertBlock(builder_);
            LLVMValueRef parentFn = LLVMGetBasicBlockParent(lhsBlock);
            LLVMBasicBlockRef rhsBlock = LLVMAppendBasicBlockInContext(ctx_, parentFn, "lor.rhs");
            LLVMBasicBlockRef endBlock = LLVMAppendBasicBlockInContext(ctx_, parentFn, "lor.end");

            LLVMBuildCondBr(builder_, lhs, endBlock, rhsBlock);

            LLVMPositionBuilderAtEnd(builder_, rhsBlock);
            LLVMValueRef rhs = genExprBool(b->rhs.get());
            LLVMBuildBr(builder_, endBlock);
            LLVMBasicBlockRef rhsEvalBlock = LLVMGetInsertBlock(builder_);

            LLVMPositionBuilderAtEnd(builder_, endBlock);
            LLVMValueRef phi = LLVMBuildPhi(builder_, bool_t_, "lor.result");

            LLVMValueRef incomingVals[2];
            LLVMBasicBlockRef incomingBlocks[2];
            incomingVals[0] = lhs;
            incomingBlocks[0] = lhsBlock;
            incomingVals[1] = rhs;
            incomingBlocks[1] = rhsEvalBlock;
            LLVMAddIncoming(phi, incomingVals, incomingBlocks, 2);

            return phi;
        }
        if (b->op == '!') {
            LLVMValueRef R = genExprBool(b->rhs.get());
            return LLVMBuildNot(builder_, R, "nottmp");
        }
        
        // Comparison operators
        if (b->op == '=' || b->op == 'n' || b->op == '<' || b->op == '>' || b->op == 'l' || b->op == 'g') {
                                                TypeInfo lhsType;
            TypeInfo rhsType;
            bool lhsTypeKnown = false;
            bool rhsTypeKnown = false;
            bool isStringComparison = false;
            bool isBooleanComparison = false;

            try {
                lhsType = inferType(b->lhs.get());
                rhsType = inferType(b->rhs.get());
                lhsTypeKnown = rhsTypeKnown = true;
                if (lhsType.type == QuarkType::String || rhsType.type == QuarkType::String)
                    isStringComparison = true;
                if (lhsType.type == QuarkType::Boolean && rhsType.type == QuarkType::Boolean)
                    isBooleanComparison = true;
                // Handle pointer/null comparisons
                if ((lhsType.type == QuarkType::Pointer || lhsType.type == QuarkType::Null) &&
                    (rhsType.type == QuarkType::Pointer || rhsType.type == QuarkType::Null)) {
                    // Pointer comparison - use genExpr and compare pointers directly
                    LLVMValueRef L = genExpr(b->lhs.get());
                    LLVMValueRef R = genExpr(b->rhs.get());
                    // Convert pointers to integers for comparison
                    LLVMTypeRef intptrType = LLVMInt64TypeInContext(ctx_);
                    LLVMValueRef Lint = LLVMBuildPtrToInt(builder_, L, intptrType, "ptr_to_int_l");
                    LLVMValueRef Rint = LLVMBuildPtrToInt(builder_, R, intptrType, "ptr_to_int_r");
                    switch (b->op) {
                        case '=': return LLVMBuildICmp(builder_, LLVMIntEQ, Lint, Rint, "eqptr");
                        case 'n': return LLVMBuildICmp(builder_, LLVMIntNE, Lint, Rint, "neptr");
                        default: throw std::runtime_error("unsupported pointer comparison operator");
                    }
                }
            } catch (...) {
                            }

                        bool lhsNamedString = false;
            bool rhsNamedString = false;
            bool lhsNamedBool = false;
            bool rhsNamedBool = false;

            if (g_named_types_) {
                

                if (auto *lhsVar = dynamic_cast<VariableExprAST *>(b->lhs.get()))
                {
                    auto typeIt = g_named_types_->find(lhsVar->name);
                    if (typeIt != g_named_types_->end())
                    {
                        if (typeIt->second == int8ptr_t_)
                            lhsNamedString = true;
                        if (typeIt->second == bool_t_)
                            lhsNamedBool = true;
                    }
                }
                if (auto *rhsVar = dynamic_cast<VariableExprAST *>(b->rhs.get()))
                {
                    auto typeIt = g_named_types_->find(rhsVar->name);
                    if (typeIt != g_named_types_->end())
                    {
                        if (typeIt->second == int8ptr_t_)
                            rhsNamedString = true;
                        if (typeIt->second == bool_t_)
                            rhsNamedBool = true;
                    }
                }

                if (!lhsTypeKnown && lhsNamedString) isStringComparison = true;
                if (!rhsTypeKnown && rhsNamedString) isStringComparison = true;
            }

            bool lhsIsBool = (lhsTypeKnown && lhsType.type == QuarkType::Boolean) || lhsNamedBool;
            bool rhsIsBool = (rhsTypeKnown && rhsType.type == QuarkType::Boolean) || rhsNamedBool;
            if (lhsIsBool && rhsIsBool)
                isBooleanComparison = true;
            
            if (isStringComparison) {
                // String comparison
                LLVMValueRef L = genExpr(b->lhs.get());
                LLVMValueRef R = genExpr(b->rhs.get());
                
                                if (auto *lhsVar = dynamic_cast<VariableExprAST *>(b->lhs.get()))
                {
                    if (g_named_values_ && g_named_types_) {
                        auto it = g_named_values_->find(lhsVar->name);
                        auto typeIt = g_named_types_->find(lhsVar->name);
                        if (it != g_named_values_->end() && typeIt != g_named_types_->end() && typeIt->second == int8ptr_t_)
                        {
                                                        auto pit = function_params_.find(lhsVar->name);
                            bool isDirectParam = (pit != function_params_.end() && pit->second);
                            
                            if (isDirectParam) {
                                L = it->second; // Use parameter directly
                            } else {
                                L = LLVMBuildLoad2(builder_, int8ptr_t_, it->second, (lhsVar->name + ".load").c_str());
                            }
                        }
                    }
                }
                if (auto *rhsVar = dynamic_cast<VariableExprAST *>(b->rhs.get()))
                {
                    if (g_named_values_ && g_named_types_) {
                        auto it = g_named_values_->find(rhsVar->name);
                        auto typeIt = g_named_types_->find(rhsVar->name);
                        if (it != g_named_values_->end() && typeIt != g_named_types_->end() && typeIt->second == int8ptr_t_)
                        {
                                                        auto pit = function_params_.find(rhsVar->name);
                            bool isDirectParam = (pit != function_params_.end() && pit->second);
                            
                            if (isDirectParam) {
                                R = it->second; // Use parameter directly
                            } else {
                                R = LLVMBuildLoad2(builder_, int8ptr_t_, it->second, (rhsVar->name + ".load").c_str());
                            }
                        }
                    }
                }
                
                                static LLVMValueRef strcmpFn = nullptr;
                if (!strcmpFn)
                {
                    strcmpFn = LLVMGetNamedFunction(module_, "strcmp");
                    if (!strcmpFn) {
                        LLVMTypeRef args[] = {int8ptr_t_, int8ptr_t_};
                        LLVMTypeRef ftype = LLVMFunctionType(int32_t_, args, 2, 0);
                        strcmpFn = LLVMAddFunction(module_, "strcmp", ftype);
                    }
                }
                
                LLVMValueRef args[] = {L, R};
                LLVMValueRef cmpResult = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strcmpFn), strcmpFn, args, 2, "strcmptmp");
                
                switch (b->op) {
                    case '=': return LLVMBuildICmp(builder_, LLVMIntEQ, cmpResult, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0), "eqtmp");
                    case 'n': return LLVMBuildICmp(builder_, LLVMIntNE, cmpResult, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0), "netmp");
                    case '<': return LLVMBuildICmp(builder_, LLVMIntSLT, cmpResult, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0), "lttmp");
                    case '>': return LLVMBuildICmp(builder_, LLVMIntSGT, cmpResult, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0), "gttmp");
                    case 'l': return LLVMBuildICmp(builder_, LLVMIntSLE, cmpResult, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0), "letmp");
                    case 'g': return LLVMBuildICmp(builder_, LLVMIntSGE, cmpResult, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 0, 0), "getmp");
                    default: throw std::runtime_error("unknown comparison operator");
                }
            } else {
                if (isBooleanComparison) {
                    LLVMValueRef L = genExprBool(b->lhs.get());
                    LLVMValueRef R = genExprBool(b->rhs.get());

                    switch (b->op) {
                        case '=': return LLVMBuildICmp(builder_, LLVMIntEQ, L, R, "eqbool");
                        case 'n': return LLVMBuildICmp(builder_, LLVMIntNE, L, R, "nebool");
                        case '<': return LLVMBuildICmp(builder_, LLVMIntULT, L, R, "ltbool");
                        case '>': return LLVMBuildICmp(builder_, LLVMIntUGT, L, R, "gtbool");
                        case 'l': return LLVMBuildICmp(builder_, LLVMIntULE, L, R, "lebool");
                        case 'g': return LLVMBuildICmp(builder_, LLVMIntUGE, L, R, "gebool");
                        default: throw std::runtime_error("unknown comparison operator");
                    }
                }
                // Integer comparison
                LLVMValueRef L = genExprInt(b->lhs.get());
                LLVMValueRef R = genExprInt(b->rhs.get());
                
                switch (b->op) {
                    case '=': return LLVMBuildICmp(builder_, LLVMIntEQ, L, R, "eqtmp");
                    case 'n': return LLVMBuildICmp(builder_, LLVMIntNE, L, R, "netmp");
                    case '<': return LLVMBuildICmp(builder_, LLVMIntSLT, L, R, "lttmp");
                    case '>': return LLVMBuildICmp(builder_, LLVMIntSGT, L, R, "gttmp");
                    case 'l': return LLVMBuildICmp(builder_, LLVMIntSLE, L, R, "letmp");
                    case 'g': return LLVMBuildICmp(builder_, LLVMIntSGE, L, R, "getmp");
                    default: throw std::runtime_error("unknown comparison operator");
                }
            }
        }
        
        throw std::runtime_error("unsupported boolean operator");
    }

        if (auto *arrayAccess = dynamic_cast<ArrayAccessExpr *>(expr))
    {
                TypeInfo elementType = inferType(expr);
        if (elementType.type == QuarkType::Boolean) {
            return genArrayAccess(arrayAccess);
        } else {
            throw std::runtime_error("array access does not return boolean type");
        }
    }

    throw std::runtime_error("unsupported boolean expression");
}

LLVMValueRef ExpressionCodeGen::genStructLiteral(StructLiteralExpr *structLiteral)
{
    if (!g_struct_types_ || !g_struct_defs_) {
        throw std::runtime_error("struct symbol tables not initialized");
    }
    
    // Find the struct type
    auto typeIt = g_struct_types_->find(structLiteral->structName);
    if (typeIt == g_struct_types_->end()) {
        throw std::runtime_error("unknown struct type: " + structLiteral->structName);
    }
    LLVMTypeRef structType = typeIt->second;
    
    // Find the struct definition
    auto defIt = g_struct_defs_->find(structLiteral->structName);
    if (defIt == g_struct_defs_->end()) {
        throw std::runtime_error("struct definition not found: " + structLiteral->structName);
    }
    const StructDefStmt* structDef = defIt->second;
    
        std::vector<std::pair<std::string, std::string>> allFields;
    std::function<void(const std::string&)> collectInheritedFields = [&](const std::string& structName) {
        auto it = g_struct_defs_->find(structName);
        if (it == g_struct_defs_->end()) {
            throw std::runtime_error("Parent struct '" + structName + "' not found for struct " + structLiteral->structName);
        }
        
        const StructDefStmt* currentStruct = it->second;
        
                if (!currentStruct->parentName.empty()) {
            collectInheritedFields(currentStruct->parentName);
        }
        
        // Add this struct's fields
        for (const auto& field : currentStruct->fields) {
            allFields.push_back(field);
        }
    };
    
        if (!structDef->parentName.empty()) {
        collectInheritedFields(structDef->parentName);
    }
    
    // Add this struct's own fields
    for (const auto& field : structDef->fields) {
        allFields.push_back(field);
    }
    
        std::unordered_map<std::string, size_t> fieldNameToIndex;
    for (size_t i = 0; i < allFields.size(); i++) {
        fieldNameToIndex[allFields[i].first] = i;
    }
    
        LLVMValueRef structAlloca = LLVMBuildAlloca(builder_, structType, "struct_tmp");
    
        for (const auto& fieldValue : structLiteral->fieldValues) {
        const std::string& fieldName = fieldValue.first;
        
                auto fieldIt = fieldNameToIndex.find(fieldName);
        if (fieldIt == fieldNameToIndex.end()) {
            throw std::runtime_error("unknown field '" + fieldName + "' in struct " + structLiteral->structName);
        }
        size_t fieldIndex = fieldIt->second;
        
        // Get the expected field type
        const std::string& fieldTypeName = allFields[fieldIndex].second;
        
                LLVMValueRef fieldVal;
        if (fieldTypeName == "str") {
            fieldVal = genExpr(fieldValue.second.get());
        } else if (fieldTypeName == "int") {
            fieldVal = genExprInt(fieldValue.second.get());
        } else if (fieldTypeName == "bool") {
            fieldVal = genExprBool(fieldValue.second.get());
        } else {
                        fieldVal = genExpr(fieldValue.second.get());
        }
        
                LLVMValueRef indices[2] = {
            LLVMConstInt(LLVMInt32Type(), 0, 0),           // struct index
            LLVMConstInt(LLVMInt32Type(), fieldIndex, 0)   // field index
        };
        LLVMValueRef fieldPtr = LLVMBuildGEP2(builder_, structType, structAlloca, indices, 2, "field_ptr");
        
        // Store the value
        LLVMBuildStore(builder_, fieldVal, fieldPtr);
    }
    
    return structAlloca;
}

LLVMValueRef ExpressionCodeGen::genMemberAccess(MemberAccessExpr *memberAccess)
{
    if (!g_struct_types_ || !g_struct_defs_ || !g_named_values_) {
        throw std::runtime_error("symbol tables not initialized");
    }
    
    // Handle nested member access (e.g., msg.author.username)
    if (auto *nestedAccess = dynamic_cast<MemberAccessExpr *>(memberAccess->object.get())) {
        // Recursively evaluate the nested access to get the struct value
        LLVMValueRef nestedValue = genMemberAccess(nestedAccess);
        
        // Get the type info of the nested field
        TypeInfo nestedTypeInfo = inferType(nestedAccess);
        if (nestedTypeInfo.type != QuarkType::Struct) {
            throw std::runtime_error("nested member access on non-struct type");
        }
        
        // Get the struct definition
        auto defIt = g_struct_defs_->find(nestedTypeInfo.structName);
        if (defIt == g_struct_defs_->end()) {
            throw std::runtime_error("struct definition not found: " + nestedTypeInfo.structName);
        }
        
        // Collect all fields including inherited ones
        std::vector<std::pair<std::string, std::string>> allFields;
        std::function<void(const std::string&)> collectInheritedFields = [&](const std::string& currentStructName) {
            auto currentIt = g_struct_defs_->find(currentStructName);
            if (currentIt == g_struct_defs_->end()) return;
            const StructDefStmt* currentStruct = currentIt->second;
            if (!currentStruct->parentName.empty()) {
                collectInheritedFields(currentStruct->parentName);
            }
            for (const auto& field : currentStruct->fields) {
                allFields.push_back(field);
            }
        };
        collectInheritedFields(nestedTypeInfo.structName);
        
        // Find the field index
        int fieldIndex = -1;
        for (size_t i = 0; i < allFields.size(); i++) {
            if (allFields[i].first == memberAccess->fieldName) {
                fieldIndex = (int)i;
                break;
            }
        }
        if (fieldIndex == -1) {
            throw std::runtime_error("field not found: " + memberAccess->fieldName + " in struct " + nestedTypeInfo.structName);
        }
        
        // Get the struct LLVM type
        auto structTypeIt = g_struct_types_->find(nestedTypeInfo.structName);
        if (structTypeIt == g_struct_types_->end()) {
            throw std::runtime_error("struct LLVM type not found: " + nestedTypeInfo.structName);
        }
        LLVMTypeRef structType = structTypeIt->second;
        
        // Check if nestedValue is a struct value or a pointer
        LLVMTypeRef nestedValueType = LLVMTypeOf(nestedValue);
        LLVMTypeKind nestedKind = LLVMGetTypeKind(nestedValueType);
        
        LLVMValueRef fieldValue;
        if (nestedKind == LLVMStructTypeKind) {
            // It's a struct value - use extractvalue
            fieldValue = LLVMBuildExtractValue(builder_, nestedValue, fieldIndex, "nested_member_val");
        } else if (nestedKind == LLVMPointerTypeKind) {
            // It's a pointer - use GEP and load
            LLVMTypeRef structPtrTy = LLVMPointerType(structType, 0);
            LLVMValueRef basePtr = LLVMBuildBitCast(builder_, nestedValue, structPtrTy, "nested_ptr_cast");
            LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structType, basePtr, fieldIndex, "nested_member_ptr");
            
            // Determine field type for load
            const std::string& fieldTypeName = allFields[fieldIndex].second;
            LLVMTypeRef fieldType;
            if (fieldTypeName == "str") {
                fieldType = int8ptr_t_;
            } else if (fieldTypeName == "int") {
                fieldType = int32_t_;
            } else if (fieldTypeName == "bool") {
                fieldType = bool_t_;
            } else {
                auto structFieldTypeIt = g_struct_types_->find(fieldTypeName);
                if (structFieldTypeIt != g_struct_types_->end()) {
                    fieldType = structFieldTypeIt->second;
                } else {
                    fieldType = int8ptr_t_; // fallback
                }
            }
            fieldValue = LLVMBuildLoad2(builder_, fieldType, fieldPtr, "nested_member_val");
        } else {
            throw std::runtime_error("unexpected nested value type kind: " + std::to_string(nestedKind));
        }
        
        return fieldValue;
    }
    
        if (auto *varExpr = dynamic_cast<VariableExprAST *>(memberAccess->object.get())) {
        // Get the struct instance
        auto it = g_named_values_->find(varExpr->name);
        if (it == g_named_values_->end()) {
            throw std::runtime_error("unknown variable: " + varExpr->name);
        }
        LLVMValueRef structPtr = it->second;
        if (verbose_) {
                        LLVMTypeRef mappedTy = LLVMTypeOf(structPtr);
            char *mappedTypeStr = LLVMPrintTypeToString(mappedTy);
            char *mappedValStr = LLVMPrintValueToString(structPtr);
            printf("[expression_codegen] lookup: var '%s' mapped value type='%s' value='%s'\n", varExpr->name.c_str(), mappedTypeStr, mappedValStr);
            LLVMDisposeMessage(mappedTypeStr);
            LLVMDisposeMessage(mappedValStr);
        }
        
                auto localIt = variableTypes_.find(varExpr->name);
        if (localIt != variableTypes_.end()) {
            const TypeInfo& typeInfo = localIt->second;
            if (typeInfo.type != QuarkType::Struct) {
                throw std::runtime_error("variable is not a struct: " + varExpr->name);
            }
            if (verbose_) {
                printf("[expression_codegen] genMemberAccess: object '%s' resolved to struct '%s', field '%s'\n",
                       varExpr->name.c_str(), typeInfo.structName.c_str(), memberAccess->fieldName.c_str());
            }
            
                        auto defIt = g_struct_defs_->find(typeInfo.structName);
            if (defIt == g_struct_defs_->end()) {
                throw std::runtime_error("struct definition not found: " + typeInfo.structName);
            }
            const StructDefStmt* structDef = defIt->second;
            
                        std::vector<std::pair<std::string, std::string>> allFields;
            std::function<void(const std::string&)> collectInheritedFields = [&](const std::string& currentStructName) {
                auto currentIt = g_struct_defs_->find(currentStructName);
                if (currentIt == g_struct_defs_->end()) {
                    return;
                }
                const StructDefStmt* currentStruct = currentIt->second;
                
                                if (!currentStruct->parentName.empty()) {
                    collectInheritedFields(currentStruct->parentName);
                }
                
                // Add this struct's fields
                for (const auto& field : currentStruct->fields) {
                    allFields.push_back(field);
                }
            };
            
            collectInheritedFields(typeInfo.structName);
            
                        int fieldIndex = -1;
            for (size_t i = 0; i < allFields.size(); i++) {
                if (allFields[i].first == memberAccess->fieldName) {
                    fieldIndex = (int)i;
                    break;
                }
            }
            
            if (fieldIndex == -1) {
                throw std::runtime_error("field not found: " + memberAccess->fieldName + " in struct " + typeInfo.structName);
            }
            
            // Get the struct LLVM type
            auto structTypeIt = g_struct_types_->find(typeInfo.structName);
            if (structTypeIt == g_struct_types_->end()) {
                throw std::runtime_error("struct LLVM type not found: " + typeInfo.structName);
            }
            LLVMTypeRef structType = structTypeIt->second;
            if (verbose_) {
                printf("[expression_codegen] genMemberAccess: struct '%s' has %zu total fields; using index %d for '%s'\n",
                       typeInfo.structName.c_str(), allFields.size(), fieldIndex, memberAccess->fieldName.c_str());
            }
            
                        LLVMValueRef fieldValue;
            
                        bool isParam = function_params_.find(varExpr->name) != function_params_.end();
            bool isDirectParam = isParam && function_params_[varExpr->name];
         if (verbose_) {
          printf("[expression_codegen] genMemberAccess: isParam=%d isDirectParam=%d for '%s'\n",
              (int)isParam, (int)isDirectParam, varExpr->name.c_str());
          LLVMTypeRef spty = LLVMTypeOf(structPtr);
          int kind = (int)LLVMGetTypeKind(spty);
          printf("[expression_codegen] genMemberAccess: structPtr LLVM type kind at entry=%d\n", kind);
          printf("[expression_codegen] genMemberAccess: structPtr == structType? %d\n", (int)(spty == structType));
          printf("[expression_codegen] genMemberAccess: isStructKind=%d isPointerKind=%d\n",
              (int)(kind == LLVMStructTypeKind), (int)(kind == LLVMPointerTypeKind));
         }
            
            if (isDirectParam) {
                                fieldValue = LLVMBuildExtractValue(builder_, structPtr, fieldIndex, "member_val");
            } else {
                                LLVMValueRef basePtr = structPtr;
                LLVMTypeRef structPtrTy = LLVMPointerType(structType, 0);

                LLVMValueRef baseAsAlloca = LLVMIsAAllocaInst(basePtr);
                if (baseAsAlloca) {
                    LLVMTypeRef allocatedTy = LLVMGetAllocatedType(basePtr);
                    if (allocatedTy == structType) {
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess: '%s' alloca stores struct value\n", varExpr->name.c_str());
                        }
                    } else if (allocatedTy == structPtrTy) {
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess: '%s' alloca stores struct pointer, loading inner pointer\n", varExpr->name.c_str());
                        }
                        basePtr = LLVMBuildLoad2(builder_, structPtrTy, basePtr, (varExpr->name + ".ptr.load").c_str());
                        baseAsAlloca = nullptr;
                    } else {
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess: '%s' alloca has unexpected allocated type, bitcasting to struct pointer\n", varExpr->name.c_str());
                        }
                        basePtr = LLVMBuildBitCast(builder_, basePtr, structPtrTy, (varExpr->name + ".ptr.alloca.cast").c_str());
                        baseAsAlloca = nullptr;
                    }
                } else {
                    LLVMTypeRef baseTy = LLVMTypeOf(basePtr);
                    if (LLVMGetTypeKind(baseTy) == LLVMStructTypeKind) {
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess: '%s' value is raw struct, storing in temporary alloca\n", varExpr->name.c_str());
                        }
                        LLVMValueRef tmpAlloca = LLVMBuildAlloca(builder_, structType, (varExpr->name + ".val.alloca").c_str());
                        LLVMBuildStore(builder_, basePtr, tmpAlloca);
                        basePtr = tmpAlloca;
                        baseAsAlloca = basePtr;
                    } else if (LLVMGetTypeKind(baseTy) == LLVMPointerTypeKind) {
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess: '%s' value is pointer-like, casting to struct pointer\n", varExpr->name.c_str());
                        }
                        basePtr = LLVMBuildBitCast(builder_, basePtr, structPtrTy, (varExpr->name + ".ptr.cast").c_str());
                    } else {
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess: '%s' value has unexpected type kind=%d, casting to struct pointer\n",
                                   varExpr->name.c_str(), (int)LLVMGetTypeKind(baseTy));
                        }
                        basePtr = LLVMBuildBitCast(builder_, basePtr, structPtrTy, (varExpr->name + ".ptr.unknown.cast").c_str());
                    }
                }

                if (!LLVMIsAAllocaInst(basePtr) && LLVMGetTypeKind(LLVMTypeOf(basePtr)) == LLVMPointerTypeKind) {
                    if (verbose_) {
                        printf("[expression_codegen] genMemberAccess: ensuring '%s' pointer is typed as struct pointer\n", varExpr->name.c_str());
                    }
                    basePtr = LLVMBuildBitCast(builder_, basePtr, structPtrTy, (varExpr->name + ".ptr.final.cast").c_str());
                }

                if (verbose_) {
                    LLVMTypeRef baseTy = LLVMTypeOf(basePtr);
                    printf("[expression_codegen] genMemberAccess: about to build GEP for field '%s' (index %d). basePtr kind=%d\n",
                           memberAccess->fieldName.c_str(), fieldIndex, (int)LLVMGetTypeKind(baseTy));
                }

                LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structType, basePtr, fieldIndex, "member_ptr");
                if (verbose_) {
                    printf("[expression_codegen] genMemberAccess: built GEP for field '%s' (index %d)\n", memberAccess->fieldName.c_str(), fieldIndex);
                }
                
                                const std::string& fieldTypeName = allFields[fieldIndex].second;
                LLVMTypeRef fieldType;
                if (fieldTypeName == "str") {
                    fieldType = int8ptr_t_;
                } else if (fieldTypeName == "int") {
                    fieldType = int32_t_;
                } else if (fieldTypeName == "bool") {
                    fieldType = bool_t_;
                } else if (fieldTypeName.size() > 2 && fieldTypeName.substr(fieldTypeName.size() - 2) == "[]") {
                                        std::string elementType = fieldTypeName.substr(0, fieldTypeName.size() - 2);
                    if (elementType == "str") {
                        fieldType = LLVMPointerType(int8ptr_t_, 0);
                    } else if (elementType == "int") {
                        fieldType = LLVMPointerType(int32_t_, 0);
                    } else if (elementType == "bool") {
                        fieldType = LLVMPointerType(bool_t_, 0);
                    } else {
                        throw std::runtime_error("unknown array element type: " + elementType);
                    }
                } else if (!fieldTypeName.empty() && fieldTypeName.back() == '*') {
                    // Pointer-typed field
                    std::string base = fieldTypeName.substr(0, fieldTypeName.size() - 1);
                    if (base == "void" || base == "char" || base == "str") {
                        fieldType = LLVMPointerType(int8ptr_t_, 0);
                    } else if (base == "int") {
                        fieldType = LLVMPointerType(int32_t_, 0);
                    } else if (base == "bool") {
                        fieldType = LLVMPointerType(bool_t_, 0);
                    } else if (base == "float") {
                        fieldType = LLVMPointerType(LLVMFloatTypeInContext(ctx_), 0);
                    } else if (base == "double") {
                        fieldType = LLVMPointerType(LLVMDoubleTypeInContext(ctx_), 0);
                    } else {
                        auto sit = g_struct_types_->find(base);
                        if (sit != g_struct_types_->end()) {
                            fieldType = LLVMPointerType(sit->second, 0);
                        } else {
                            fieldType = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
                        }
                    }
                } else {
                    // It's a struct type
                    auto structFieldTypeIt = g_struct_types_->find(fieldTypeName);
                    if (structFieldTypeIt != g_struct_types_->end()) {
                        fieldType = structFieldTypeIt->second;
                    } else {
                        throw std::runtime_error("unknown field type: " + fieldTypeName);
                    }
                }
                
                // Load the field value
                fieldValue = LLVMBuildLoad2(builder_, fieldType, fieldPtr, "member_val");
            }
            
            return fieldValue;
        } else {
                        if (!g_named_types_) {
                throw std::runtime_error("named types not initialized");
            }
            auto typeIt = g_named_types_->find(varExpr->name);
            if (typeIt == g_named_types_->end()) {
                throw std::runtime_error("type info not found for variable: " + varExpr->name);
            }
            
                                                LLVMTypeRef structType = typeIt->second;
            if (LLVMGetTypeKind(structType) != LLVMStructTypeKind) {
                if (LLVMGetTypeKind(structType) == LLVMPointerTypeKind) {
                    LLVMTypeRef elemTy = LLVMGetElementType(structType);
                    if (elemTy && LLVMGetTypeKind(elemTy) == LLVMStructTypeKind) {
                        structType = elemTy;                     } else {
                        throw std::runtime_error("variable '" + varExpr->name + "' is not a struct type");
                    }
                } else {
                    throw std::runtime_error("variable '" + varExpr->name + "' is not a struct type");
                }
            }
            
                        std::string structName;
            if (g_struct_types_) {
                for (const auto& structPair : *g_struct_types_) {
                    if (structPair.second == structType) {
                        structName = structPair.first;
                        break;
                    }
                }
            }
            
            if (structName.empty()) {
                throw std::runtime_error("could not find struct name for LLVM type");
            }
            
                        auto defIt = g_struct_defs_->find(structName);
            if (defIt == g_struct_defs_->end()) {
                throw std::runtime_error("struct definition not found: " + structName);
            }
            const StructDefStmt* structDef = defIt->second;
            
                        std::vector<std::pair<std::string, std::string>> allFields;
            std::function<void(const std::string&)> collectInheritedFields = [&](const std::string& currentStructName) {
                auto currentIt = g_struct_defs_->find(currentStructName);
                if (currentIt == g_struct_defs_->end()) {
                    return;
                }
                const StructDefStmt* currentStruct = currentIt->second;
                
                                if (!currentStruct->parentName.empty()) {
                    collectInheritedFields(currentStruct->parentName);
                }
                
                // Add this struct's fields
                for (const auto& field : currentStruct->fields) {
                    allFields.push_back(field);
                }
            };
            
            collectInheritedFields(structName);
            
                        int fieldIndex = -1;
            for (size_t i = 0; i < allFields.size(); i++) {
                if (allFields[i].first == memberAccess->fieldName) {
                    fieldIndex = (int)i;
                    break;
                }
            }
            
            if (fieldIndex == -1) {
                throw std::runtime_error("field not found: " + memberAccess->fieldName + " in struct " + structName);
            }
            
            // Use GEP to access the field
                        LLVMValueRef basePtr2 = structPtr;
            if (auto *objVar = dynamic_cast<VariableExprAST *>(memberAccess->object.get())) {
                if (objVar->name == "this") {
                    LLVMTypeRef baseTy2 = LLVMTypeOf(basePtr2);
                    if (LLVMGetTypeKind(baseTy2) == LLVMPointerTypeKind) {
                        LLVMTypeRef elemTy2 = LLVMGetElementType(baseTy2);
                        if (LLVMGetTypeKind(elemTy2) == LLVMPointerTypeKind && LLVMGetElementType(elemTy2) == structType) {
                            basePtr2 = LLVMBuildLoad2(builder_, elemTy2, basePtr2, "this.ptr.load");
                        } else if (elemTy2 == structType) {
                            // already pointer-to-struct
                        } else {
                            LLVMTypeRef desiredPtr2 = LLVMPointerType(structType, 0);
                            if (verbose_) {
                                printf("[expression_codegen] genMemberAccess (this-path2): about to bitcast basePtr2 to struct ptr. basePtr2 type kind=%d\n",
                                       (int)LLVMGetTypeKind(LLVMTypeOf(basePtr2)));
                            }
                            basePtr2 = LLVMBuildBitCast(builder_, basePtr2, desiredPtr2, "this.ptr.cast");
                        }
                    } else if (LLVMGetTypeKind(baseTy2) == LLVMStructTypeKind) {
                        LLVMValueRef tmpAlloca2 = LLVMBuildAlloca(builder_, structType, "this.val.alloca");
                        LLVMBuildStore(builder_, basePtr2, tmpAlloca2);
                        basePtr2 = tmpAlloca2;
                    } else {
                        LLVMTypeRef desiredPtr2 = LLVMPointerType(structType, 0);
                        if (verbose_) {
                            printf("[expression_codegen] genMemberAccess (this-path2): fallback bitcast basePtr2 to struct ptr. basePtr2 type kind=%d\n",
                                   (int)LLVMGetTypeKind(LLVMTypeOf(basePtr2)));
                        }
                        basePtr2 = LLVMBuildBitCast(builder_, basePtr2, desiredPtr2, "this.ptr.cast_fallback");
                    }
                } else {
                    LLVMTypeRef spTy2 = LLVMTypeOf(basePtr2);
                                        if (LLVMGetTypeKind(spTy2) == LLVMStructTypeKind) {
                        LLVMValueRef tmpAlloca = LLVMBuildAlloca(builder_, structType, (objVar->name + ".val.alloca").c_str());
                        LLVMBuildStore(builder_, basePtr2, tmpAlloca);
                        basePtr2 = tmpAlloca;
                        spTy2 = LLVMTypeOf(basePtr2);
                        if (verbose_) printf("[expression_codegen] genMemberAccess: stored struct value into alloca for '%s'\n", objVar->name.c_str());
                    }
                                        if (LLVMGetTypeKind(spTy2) != LLVMPointerTypeKind && LLVMGetTypeKind(spTy2) != LLVMStructTypeKind) {
                        if (verbose_) printf("[expression_codegen] genMemberAccess: unexpected base type kind=%d for '%s', attempting fallback bitcast2\n", (int)LLVMGetTypeKind(spTy2), objVar->name.c_str());
                        LLVMTypeRef desiredPtr2 = LLVMPointerType(structType, 0);
                        basePtr2 = LLVMBuildBitCast(builder_, basePtr2, desiredPtr2, "member_base_cast2_fallback");
                        spTy2 = LLVMTypeOf(basePtr2);
                    }
                    if (LLVMGetTypeKind(spTy2) == LLVMPointerTypeKind) {
                        LLVMTypeRef elemTy2 = LLVMGetElementType(spTy2);
                        if (LLVMGetTypeKind(elemTy2) == LLVMPointerTypeKind && LLVMGetElementType(elemTy2) == structType) {
                            basePtr2 = LLVMBuildLoad2(builder_, elemTy2, basePtr2, "this.ptr.load");
                        } else if (elemTy2 != structType) {
                                                        LLVMTypeRef desiredPtr2 = LLVMPointerType(structType, 0);
                            basePtr2 = LLVMBuildBitCast(builder_, basePtr2, desiredPtr2, "member_base_cast2");
                        }
                    }
                }
            }
            if (verbose_) {
                LLVMTypeRef preTy2 = LLVMTypeOf(basePtr2);
                LLVMTypeRef preElem2 = (LLVMGetTypeKind(preTy2) == LLVMPointerTypeKind) ? LLVMGetElementType(preTy2) : nullptr;
                int preElemKind2 = preElem2 ? (int)LLVMGetTypeKind(preElem2) : -1;
                printf("[expression_codegen] pre-GEP-2: basePtr2 type kind=%d, elem kind=%d, elem==structType? %d, basePtr2==ptrToStruct? %d\n",
                       (int)LLVMGetTypeKind(preTy2), preElemKind2,
                       (int)(preElem2 == structType), (int)(preTy2 == LLVMPointerType(structType, 0)));
            }
            LLVMValueRef fieldPtr = LLVMBuildStructGEP2(builder_, structType, basePtr2, fieldIndex, "member_ptr");
            
                        const std::string& fieldTypeName = allFields[fieldIndex].second;
            LLVMTypeRef fieldType;
            if (fieldTypeName == "str") {
                fieldType = int8ptr_t_;
            } else if (fieldTypeName == "int") {
                fieldType = int32_t_;
            } else if (fieldTypeName == "bool") {
                fieldType = bool_t_;
            } else if (fieldTypeName.size() > 2 && fieldTypeName.substr(fieldTypeName.size() - 2) == "[]") {
                                std::string elementType = fieldTypeName.substr(0, fieldTypeName.size() - 2);
                if (elementType == "str") {
                    fieldType = LLVMPointerType(int8ptr_t_, 0);
                } else if (elementType == "int") {
                    fieldType = LLVMPointerType(int32_t_, 0);
                } else if (elementType == "bool") {
                    fieldType = LLVMPointerType(bool_t_, 0);
                } else {
                    throw std::runtime_error("unknown array element type: " + elementType);
                }
            } else if (!fieldTypeName.empty() && fieldTypeName.back() == '*') {
                // Pointer-typed field
                std::string base = fieldTypeName.substr(0, fieldTypeName.size() - 1);
                if (base == "void" || base == "char" || base == "str") {
                    fieldType = LLVMPointerType(int8ptr_t_, 0);
                } else if (base == "int") {
                    fieldType = LLVMPointerType(int32_t_, 0);
                } else if (base == "bool") {
                    fieldType = LLVMPointerType(bool_t_, 0);
                } else if (base == "float") {
                    fieldType = LLVMPointerType(LLVMFloatTypeInContext(ctx_), 0);
                } else if (base == "double") {
                    fieldType = LLVMPointerType(LLVMDoubleTypeInContext(ctx_), 0);
                } else {
                    auto sit = g_struct_types_->find(base);
                    if (sit != g_struct_types_->end()) {
                        fieldType = LLVMPointerType(sit->second, 0);
                    } else {
                        fieldType = LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0);
                    }
                }
            } else {
                // It's a struct type
                auto structFieldTypeIt = g_struct_types_->find(fieldTypeName);
                if (structFieldTypeIt != g_struct_types_->end()) {
                    fieldType = structFieldTypeIt->second;
                } else {
                    throw std::runtime_error("unknown field type: " + fieldTypeName);
                }
            }
            
            // Load the field value
            LLVMValueRef fieldValue = LLVMBuildLoad2(builder_, fieldType, fieldPtr, "member_val");
            return fieldValue;
        }
    } else {
        throw std::runtime_error("complex member access expressions not yet supported");
    }
}

LLVMValueRef ExpressionCodeGen::genMethodCall(MethodCallExpr *methodCall)
{
    if (verbose_)
        printf("[codegen] generating method call on expression: %s\n", methodCall->methodName.c_str());
    
        LLVMValueRef objectValue = genExpr(methodCall->object.get());
    if (!objectValue) {
        throw std::runtime_error("Failed to generate object expression for method call");
    }
    
        TypeInfo objType = inferType(methodCall->object.get());
    if (objType.type == QuarkType::Array) {
            std::vector<LLVMValueRef> args;
                LLVMValueRef asI8 = LLVMBuildPointerCast(builder_, objectValue, int8ptr_t_, "arr_i8");
        // Handle supported methods
        if (methodCall->methodName == "count" || methodCall->methodName == "length") {
            args.push_back(asI8);
            return builtinFunctions_->generateBuiltinCall("array_length", args);
        }
        if (methodCall->methodName == "push") {
            args.push_back(asI8);
            // elem pointer and elemSize
            if (methodCall->args.size() != 2)
                throw CodeGenError("array.push expects 2 arguments: &elem, elemSize", methodCall->location);
            LLVMValueRef elemPtr = genExpr(methodCall->args[0].get());
            LLVMValueRef elemPtrI8 = LLVMBuildPointerCast(builder_, elemPtr, int8ptr_t_, "elem_i8");
            LLVMValueRef elemSize = genExprInt(methodCall->args[1].get());
            args.push_back(elemPtrI8);
            args.push_back(elemSize);
            return builtinFunctions_->generateBuiltinCall("array_push", args);
        }
        if (methodCall->methodName == "slice") {
            args.push_back(asI8);
            if (methodCall->args.size() != 3)
                throw CodeGenError("array.slice expects 3 arguments: start, end, elemSize", methodCall->location);
            LLVMValueRef start = genExprInt(methodCall->args[0].get());
            LLVMValueRef end = genExprInt(methodCall->args[1].get());
            LLVMValueRef elemSize = genExprInt(methodCall->args[2].get());
            args.push_back(start);
            args.push_back(end);
            args.push_back(elemSize);
            return builtinFunctions_->generateBuiltinCall("array_slice", args);
        }
        if (methodCall->methodName == "pop") {
            args.push_back(asI8);
            if (methodCall->args.size() != 1)
                throw CodeGenError("array.pop expects 1 argument: elemSize", methodCall->location);
            LLVMValueRef elemSize = genExprInt(methodCall->args[0].get());
            args.push_back(elemSize);
            return builtinFunctions_->generateBuiltinCall("array_pop", args);
        }
        if (methodCall->methodName == "free") {
            args.push_back(asI8);
            return builtinFunctions_->generateBuiltinCall("array_free", args);
        }
        throw CodeGenError("Unknown array method: " + methodCall->methodName, methodCall->location);
    }

        if (objType.type == QuarkType::Struct) {
        // Resolve struct type name
        std::string structName = objType.structName;
        if (structName.empty()) {
                        LLVMTypeRef objLLVMType = LLVMTypeOf(objectValue);
            if (g_struct_types_) {
                for (const auto &p : *g_struct_types_) {
                    if (p.second == objLLVMType) { structName = p.first; break; }
                }
            }
        }

        if (structName.empty()) {
            throw CodeGenError("could not resolve struct type for instance method call", methodCall->location);
        }

                if (!g_function_map_ || !g_struct_defs_) {
            throw CodeGenError("internal: function map or struct defs not initialized", methodCall->location);
        }

        std::string targetStructName = structName;
        std::string mangledName = targetStructName + "::" + methodCall->methodName;
        auto findIt = g_function_map_->find(mangledName);
        while (findIt == g_function_map_->end()) {
                        auto defIt = g_struct_defs_->find(targetStructName);
            if (defIt == g_struct_defs_->end()) {
                break;
            }
            const StructDefStmt* def = defIt->second;
            if (def->parentName.empty()) {
                break;
            }
            targetStructName = def->parentName;
            mangledName = targetStructName + "::" + methodCall->methodName;
            findIt = g_function_map_->find(mangledName);
        }

        if (findIt == g_function_map_->end()) {
            // Not found in hierarchy
            std::string triedName = structName + "::" + methodCall->methodName;
            throw CodeGenError("instance method '" + triedName + "' not found (and no inherited method found)", methodCall->location);
        }

    LLVMValueRef function = findIt->second;

        if (verbose_)
            printf("[codegen] genMethodCall: calling %s with %zu user args\n", mangledName.c_str(), methodCall->args.size());

    std::vector<LLVMValueRef> args;

                LLVMTypeRef fnType = LLVMGlobalGetValueType(function);
        unsigned paramCount = LLVMCountParamTypes(fnType);
        if (paramCount == 0) {
            throw CodeGenError("method '" + mangledName + "' has no parameters (expected self)", methodCall->location);
        }
        std::vector<LLVMTypeRef> expectedParams(paramCount);
        LLVMGetParamTypes(fnType, expectedParams.data());

        LLVMTypeRef expectedSelfType = expectedParams[0];

                LLVMTypeRef objStructTy = nullptr;
        if (objType.type == QuarkType::Struct && g_struct_types_) {
            auto it = g_struct_types_->find(structName);
            if (it != g_struct_types_->end()) objStructTy = it->second;
        }
        if (!objStructTy) {
            throw CodeGenError("failed to resolve LLVM struct type for 'self'", methodCall->location);
        }

        LLVMValueRef objectPtr = nullptr;

                if (auto *varExprObj = dynamic_cast<VariableExprAST *>(methodCall->object.get())) {
            if (g_named_values_) {
                auto namedIt = g_named_values_->find(varExprObj->name);
                if (namedIt != g_named_values_->end()) {
                    LLVMValueRef storedVal = namedIt->second;
                    LLVMValueRef allocaInst = LLVMIsAAllocaInst(storedVal);
                    if (allocaInst) {
                        LLVMTypeRef allocatedTy = LLVMGetAllocatedType(allocaInst);
                        if (allocatedTy) {
                            LLVMTypeKind allocatedKind = LLVMGetTypeKind(allocatedTy);
                            if (allocatedKind == LLVMStructTypeKind) {
                                if (verbose_)
                                    printf("[codegen] genMethodCall using original alloca for '%s'\n", varExprObj->name.c_str());
                                objectPtr = storedVal;
                            } else if (allocatedKind == LLVMPointerTypeKind) {
                                if (verbose_)
                                    printf("[codegen] genMethodCall loading pointer for '%s'\n", varExprObj->name.c_str());
                                objectPtr = LLVMBuildLoad2(builder_, allocatedTy, storedVal, "self.ptr.named");
                            }
                        }
                    } else {
                        LLVMTypeRef valTy = LLVMTypeOf(storedVal);
                        if (LLVMGetTypeKind(valTy) == LLVMPointerTypeKind) {
                            LLVMTypeRef desiredPtr = LLVMPointerType(objStructTy, 0);
                            objectPtr = (valTy == desiredPtr)
                                ? storedVal
                                : LLVMBuildBitCast(builder_, storedVal, desiredPtr, "self.ptr.param.cast");
                        }
                    }
                }
            }
        }

                if (!objectPtr) {
            if (verbose_)
                printf("[codegen] genMethodCall falling back to temporary for method '%s'\n", methodCall->methodName.c_str());
            if (LLVMGetTypeKind(LLVMTypeOf(objectValue)) == LLVMPointerTypeKind) {
                                LLVMTypeRef desiredPtrTy = LLVMPointerType(objStructTy, 0);
                if (LLVMTypeOf(objectValue) != desiredPtrTy) {
                    objectPtr = LLVMBuildBitCast(builder_, objectValue, desiredPtrTy, "self.ptr.cast");
                } else {
                    objectPtr = objectValue;
                }
            } else {
                LLVMValueRef objAlloca = LLVMBuildAlloca(builder_, objStructTy, "self.tmp");
                LLVMBuildStore(builder_, objectValue, objAlloca);
                objectPtr = objAlloca;
            }
        }

                        LLVMValueRef coercedSelfPtr = objectPtr;
    if (expectedSelfType != LLVMPointerType(objStructTy, 0)) {
                        std::string expectedStructName;
            if (g_struct_types_) {
                for (const auto &p : *g_struct_types_) {
                    if (LLVMPointerType(p.second, 0) == expectedSelfType) { expectedStructName = p.first; break; }
                }
            }
            if (expectedStructName.empty()) {
                throw CodeGenError("failed to resolve expected self struct type for method '" + mangledName + "'", methodCall->location);
            }

                        bool derives = false;
            std::string cur = structName;
            while (true) {
                if (cur == expectedStructName) { derives = true; break; }
                auto it = g_struct_defs_->find(cur);
                if (it == g_struct_defs_->end() || it->second->parentName.empty()) break;
                cur = it->second->parentName;
            }
            if (!derives) {
                throw CodeGenError("cannot pass '" + structName + "' as '" + expectedStructName + "' for method '" + mangledName + "'", methodCall->location);
            }

                        coercedSelfPtr = LLVMBuildBitCast(builder_, objectPtr, expectedSelfType, "self.to.parent");
        }

        // Push self first
        args.push_back(coercedSelfPtr);

                        LLVMValueRef dynName = nullptr;
        auto dynIt = g_named_values_ ? g_named_values_->find("__dyn_type_name") : g_named_values_->end();
        if (g_named_values_ && dynIt != g_named_values_->end()) {
                        dynName = LLVMBuildLoad2(builder_, int8ptr_t_, dynIt->second, "dyn_type_name.load");
        } else {
                        dynName = LLVMBuildGlobalStringPtr(builder_, structName.c_str(), "dyn_type_name");
        }
        args.push_back(dynName);

                if (g_function_param_types_) {
            auto paramTypesIt = g_function_param_types_->find(mangledName);
            if (paramTypesIt != g_function_param_types_->end()) {
                const auto &expected = paramTypesIt->second;
                for (size_t i = 0; i < methodCall->args.size(); ++i) {
                    size_t paramIndex = 2 + i;                     LLVMValueRef argVal;
                    if (paramIndex < expected.size()) {
                        if (isLLVMIntegerType(expected[paramIndex])) {
                            argVal = genExprIntWithType(methodCall->args[i].get(), expected[paramIndex]);
                        } else if (expected[paramIndex] == bool_t_) {
                            argVal = genExprBool(methodCall->args[i].get());
                        } else {
                            argVal = genExpr(methodCall->args[i].get());
                        }
                    } else {
                        argVal = genExpr(methodCall->args[i].get());
                    }
                    args.push_back(argVal);
                }
            } else {
                for (size_t i = 0; i < methodCall->args.size(); ++i) {
                    args.push_back(genExpr(methodCall->args[i].get()));
                }
            }
        } else {
            for (size_t i = 0; i < methodCall->args.size(); ++i) {
                args.push_back(genExpr(methodCall->args[i].get()));
            }
        }

                    if (auto *varExprObj = dynamic_cast<VariableExprAST *>(methodCall->object.get())) {
            if (varExprObj->name == "this" && g_named_values_ && g_function_map_) {
                bool haveDyn = (g_named_values_->find("__dyn_type_name") != g_named_values_->end());
                if (haveDyn) {
                                        std::vector<std::string> candidates;
                    for (const auto &p : *g_function_map_) {
                        const std::string &fname = p.first;
                        size_t pos = fname.find("::");
                        if (pos == std::string::npos) continue;
                        std::string sname = fname.substr(0, pos);
                        std::string mname = fname.substr(pos + 2);
                        if (mname != methodCall->methodName) continue;
                                                std::string cur = sname;
                        bool derives = false;
                        while (true) {
                            if (cur == targetStructName) { derives = true; break; }
                            auto it = g_struct_defs_->find(cur);
                            if (it == g_struct_defs_->end() || it->second->parentName.empty()) break;
                            cur = it->second->parentName;
                        }
                        if (derives) candidates.push_back(sname);
                    }

                                                            LLVMValueRef currentFnVal = LLVMGetBasicBlockParent(LLVMGetInsertBlock(builder_));
                    LLVMBasicBlockRef mergeBB = LLVMAppendBasicBlockInContext(ctx_, currentFnVal, "dyn_dispatch_merge");

                                        LLVMTypeRef retTy = LLVMGetReturnType(fnType);
                    bool isVoid = (LLVMGetTypeKind(retTy) == LLVMVoidTypeKind);
                    LLVMValueRef retAlloca = nullptr;
                    if (!isVoid) {
                        retAlloca = LLVMBuildAlloca(builder_, retTy, "dyn_ret");
                    }

                                        static LLVMValueRef strcmpFn = nullptr;
                    if (!strcmpFn) {
                        strcmpFn = LLVMGetNamedFunction(module_, "strcmp");
                        if (!strcmpFn) {
                            LLVMTypeRef argsTys[] = { int8ptr_t_, int8ptr_t_ };
                            LLVMTypeRef ftype = LLVMFunctionType(int32_t_, argsTys, 2, 0);
                            strcmpFn = LLVMAddFunction(module_, "strcmp", ftype);
                        }
                    }

                                        auto genCallForStruct = [&](const std::string &targetStruct) {
                        std::string targetName = targetStruct + "::" + methodCall->methodName;
                        auto fit = g_function_map_->find(targetName);
                        if (fit == g_function_map_->end()) {
                            // Function not found - still need to terminate basic block
                            LLVMBuildBr(builder_, mergeBB);
                            return;
                        }
                        LLVMValueRef callee = fit->second;
                        LLVMTypeRef calleeTy = LLVMGlobalGetValueType(callee);
                                                unsigned pc = LLVMCountParamTypes(calleeTy);
                        std::vector<LLVMTypeRef> exp(pc);
                        LLVMGetParamTypes(calleeTy, exp.data());
                        LLVMTypeRef expSelfTy = (pc > 0) ? exp[0] : LLVMPointerType(objStructTy, 0);
                                                LLVMValueRef selfForCall = coercedSelfPtr;
                        if (LLVMTypeOf(selfForCall) != expSelfTy) {
                            selfForCall = LLVMBuildBitCast(builder_, objectPtr, expSelfTy, "self.to.target");
                        }
                        std::vector<LLVMValueRef> callArgs;
                        callArgs.push_back(selfForCall);
                        // Hidden dyn name next
                        callArgs.push_back(dynName);
                        // User args with type guidance
                        if (g_function_param_types_) {
                            auto pti = g_function_param_types_->find(targetName);
                            if (pti != g_function_param_types_->end()) {
                                const auto &expected = pti->second;
                                for (size_t i = 0; i < methodCall->args.size(); ++i) {
                                    size_t paramIndex = 2 + i;
                                    LLVMValueRef a;
                                    if (paramIndex < expected.size()) {
                                        if (isLLVMIntegerType(expected[paramIndex])) a = genExprIntWithType(methodCall->args[i].get(), expected[paramIndex]);
                                        else if (expected[paramIndex] == bool_t_) a = genExprBool(methodCall->args[i].get());
                                        else a = genExpr(methodCall->args[i].get());
                                    } else {
                                        a = genExpr(methodCall->args[i].get());
                                    }
                                    callArgs.push_back(a);
                                }
                            } else {
                                for (size_t i = 0; i < methodCall->args.size(); ++i) callArgs.push_back(genExpr(methodCall->args[i].get()));
                            }
                        } else {
                            for (size_t i = 0; i < methodCall->args.size(); ++i) callArgs.push_back(genExpr(methodCall->args[i].get()));
                        }
                        LLVMValueRef cv = LLVMBuildCall2(builder_, calleeTy, callee, callArgs.data(), (unsigned)callArgs.size(), isVoid ? "" : "call_dyn");
                        if (!isVoid) LLVMBuildStore(builder_, cv, retAlloca);
                        LLVMBuildBr(builder_, mergeBB);
                    };

                    // Build chain of comparisons
                    LLVMBasicBlockRef nextBB = nullptr;
                                        for (const auto &cand : candidates) {
                        if (cand == structName) continue;
                        // Compare dynName to cand
                        LLVMBasicBlockRef curBB = LLVMGetInsertBlock(builder_);
                        LLVMValueRef fnVal = LLVMGetBasicBlockParent(curBB);
                        LLVMBasicBlockRef thenBB = LLVMAppendBasicBlockInContext(ctx_, fnVal, "dyn_then");
                        LLVMBasicBlockRef contBB = LLVMAppendBasicBlockInContext(ctx_, fnVal, "dyn_cont");
                        // strcmp(dynName, "cand") == 0
                        LLVMValueRef candStr = LLVMBuildGlobalStringPtr(builder_, cand.c_str(), "dyn_cand");
                        LLVMValueRef sargs[] = { dynName, candStr };
                        LLVMValueRef cmpRes = LLVMBuildCall2(builder_, LLVMGlobalGetValueType(strcmpFn), strcmpFn, sargs, 2, "dyn_strcmp");
                        LLVMValueRef zero = LLVMConstInt(int32_t_, 0, 0);
                        LLVMValueRef isEq = LLVMBuildICmp(builder_, LLVMIntEQ, cmpRes, zero, "dyn_is_eq");
                        LLVMBuildCondBr(builder_, isEq, thenBB, contBB);
                        // then: call target override
                        LLVMPositionBuilderAtEnd(builder_, thenBB);
                        genCallForStruct(cand);
                                                LLVMPositionBuilderAtEnd(builder_, contBB);
                        nextBB = contBB;
                    }

                                        genCallForStruct(targetStructName);

                    // Merge
                    LLVMPositionBuilderAtEnd(builder_, mergeBB);
                    if (!isVoid) {
                        LLVMValueRef rv = LLVMBuildLoad2(builder_, retTy, retAlloca, "dyn_rv");
                        return rv;
                    } else {
                                                return LLVMConstInt(int32_t_, 0, 0);
                    }
                }
            }
        }

                LLVMTypeRef retTy = LLVMGetReturnType(fnType);
        bool isVoid = (LLVMGetTypeKind(retTy) == LLVMVoidTypeKind);
                LLVMValueRef call = LLVMBuildCall2(builder_, fnType, function, args.data(), (unsigned)args.size(), isVoid ? "" : "call");
        return call;
    }

        if (objType.type == QuarkType::String) {
        std::vector<LLVMValueRef> args;
                LLVMValueRef strVal = objectValue;
        LLVMTypeRef ty = LLVMTypeOf(strVal);
        if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind) {
            LLVMTypeRef elem = LLVMGetElementType(ty);
            if (elem == int8ptr_t_) {
                strVal = LLVMBuildLoad2(builder_, int8ptr_t_, strVal, "str.load");
            }
        }
        args.push_back(strVal);

        // Add method arguments
        for (const auto &arg : methodCall->args) {
            LLVMValueRef argValue = genExpr(arg.get());
            if (!argValue) throw std::runtime_error("Failed to generate method call argument");
            args.push_back(argValue);
        }

        std::string builtinName = "str_" + methodCall->methodName;
        if (verbose_) printf("[codegen] calling builtin function: %s\n", builtinName.c_str());
        return builtinFunctions_->generateBuiltinCall(builtinName, args);
    }
    
    throw std::runtime_error("Method calls are currently only supported on string types");
}

LLVMValueRef ExpressionCodeGen::genStaticCall(StaticCallExpr *staticCall)
{
    if (verbose_)
        printf("[codegen] generating static method call %s::%s\n", staticCall->structName.c_str(), staticCall->methodName.c_str());
    
    auto varIt = variableTypes_.find(staticCall->structName);
    if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::Array) {
        if (verbose_)
            printf("[codegen] genStaticCall: treating as array method call on '%s'\n", staticCall->structName.c_str());
        
        VariableExprAST varExpr(staticCall->structName);
        LLVMValueRef objectValue = genExpr(&varExpr);
        
        std::vector<LLVMValueRef> args;
        LLVMValueRef asI8 = LLVMBuildPointerCast(builder_, objectValue, int8ptr_t_, "arr_i8");
        
        if (staticCall->methodName == "count" || staticCall->methodName == "length") {
            args.push_back(asI8);
            return builtinFunctions_->generateBuiltinCall("array_length", args);
        }
        if (staticCall->methodName == "push") {
            args.push_back(asI8);
            if (staticCall->args.size() != 2)
                throw CodeGenError("array.push expects 2 arguments: &elem, elemSize", staticCall->location);
            LLVMValueRef elemPtr = genExpr(staticCall->args[0].get());
            LLVMValueRef elemPtrI8 = LLVMBuildPointerCast(builder_, elemPtr, int8ptr_t_, "elem_i8");
            LLVMValueRef elemSize = genExprInt(staticCall->args[1].get());
            args.push_back(elemPtrI8);
            args.push_back(elemSize);
            return builtinFunctions_->generateBuiltinCall("array_push", args);
        }
        if (staticCall->methodName == "slice") {
            args.push_back(asI8);
            if (staticCall->args.size() != 3)
                throw CodeGenError("array.slice expects 3 arguments: start, end, elemSize", staticCall->location);
            LLVMValueRef start = genExprInt(staticCall->args[0].get());
            LLVMValueRef end = genExprInt(staticCall->args[1].get());
            LLVMValueRef elemSize = genExprInt(staticCall->args[2].get());
            args.push_back(start);
            args.push_back(end);
            args.push_back(elemSize);
            return builtinFunctions_->generateBuiltinCall("array_slice", args);
        }
        if (staticCall->methodName == "pop") {
            args.push_back(asI8);
            if (staticCall->args.size() != 1)
                throw CodeGenError("array.pop expects 1 argument: elemSize", staticCall->location);
            LLVMValueRef elemSize = genExprInt(staticCall->args[0].get());
            args.push_back(elemSize);
            return builtinFunctions_->generateBuiltinCall("array_pop", args);
        }
        if (staticCall->methodName == "free") {
            args.push_back(asI8);
            return builtinFunctions_->generateBuiltinCall("array_free", args);
        }
        throw std::runtime_error("Unknown array method: " + staticCall->methodName);
    }
    
    if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::String) {
        if (verbose_)
            printf("[codegen] genStaticCall: treating as string method call on '%s'\n", staticCall->structName.c_str());
        
        VariableExprAST varExpr(staticCall->structName);
        LLVMValueRef objectValue = genExpr(&varExpr);
        
        std::vector<LLVMValueRef> args;
        args.push_back(objectValue);
        
        if (staticCall->methodName == "length") {
            return builtinFunctions_->generateBuiltinCall("str_len", args);
        }
        if (staticCall->methodName == "slice") {
            if (staticCall->args.size() != 2)
                throw CodeGenError("string.slice expects 2 arguments: start, end", staticCall->location);
            LLVMValueRef start = genExprInt(staticCall->args[0].get());
            LLVMValueRef end = genExprInt(staticCall->args[1].get());
            args.push_back(start);
            args.push_back(end);
            return builtinFunctions_->generateBuiltinCall("str_slice", args);
        }
        if (staticCall->methodName == "find") {
            if (staticCall->args.size() != 1)
                throw CodeGenError("string.find expects 1 argument: needle", staticCall->location);
            LLVMValueRef needle = genExpr(staticCall->args[0].get());
            args.push_back(needle);
            return builtinFunctions_->generateBuiltinCall("str_find", args);
        }
        if (staticCall->methodName == "replace") {
            if (staticCall->args.size() != 2)
                throw CodeGenError("string.replace expects 2 arguments: old, new", staticCall->location);
            LLVMValueRef oldStr = genExpr(staticCall->args[0].get());
            LLVMValueRef newStr = genExpr(staticCall->args[1].get());
            args.push_back(oldStr);
            args.push_back(newStr);
            return builtinFunctions_->generateBuiltinCall("str_replace", args);
        }
        if (staticCall->methodName == "split") {
            if (staticCall->args.size() != 1)
                throw CodeGenError("string.split expects 1 argument: delimiter", staticCall->location);
            LLVMValueRef delim = genExpr(staticCall->args[0].get());
            args.push_back(delim);
            return builtinFunctions_->generateBuiltinCall("str_split", args);
        }
        throw std::runtime_error("Unknown string method: " + staticCall->methodName);
    }
    
            std::string actualStructType = staticCall->structName;
    
        if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::Struct) {
                actualStructType = varIt->second.structName;
        if (verbose_)
            printf("[codegen] genStaticCall: resolved variable '%s' to struct type '%s'\n", 
                   staticCall->structName.c_str(), actualStructType.c_str());
    } else if (varIt != variableTypes_.end() && varIt->second.type == QuarkType::Pointer) {
                actualStructType = varIt->second.structName;
        if (verbose_)
            printf("[codegen] genStaticCall: resolved pointer variable '%s' to struct type '%s'\n", 
                   staticCall->structName.c_str(), actualStructType.c_str());
    } else if (g_named_types_) {
        auto globalIt = g_named_types_->find(staticCall->structName);
        if (globalIt != g_named_types_->end()) {
                        if (g_struct_types_) {
                for (const auto& structPair : *g_struct_types_) {
                    if (structPair.second == globalIt->second) {
                        actualStructType = structPair.first;
                        if (verbose_)
                            printf("[codegen] genStaticCall: resolved global variable '%s' to struct type '%s'\n", 
                                   staticCall->structName.c_str(), actualStructType.c_str());
                        break;
                    }
                }
            }
        }
    }
    
        std::string targetStructType = actualStructType;
    std::string mangledName = targetStructType + "::" + staticCall->methodName;
    
        auto it = g_function_map_->find(mangledName);
    while (it == g_function_map_->end() && g_struct_defs_) {
        auto defIt = g_struct_defs_->find(targetStructType);
        if (defIt == g_struct_defs_->end()) {
            break;
        }
        const StructDefStmt* def = defIt->second;
        if (def->parentName.empty()) {
            break;
        }
        targetStructType = def->parentName;
        mangledName = targetStructType + "::" + staticCall->methodName;
        it = g_function_map_->find(mangledName);
    }
    
    if (it == g_function_map_->end()) {
        throw std::runtime_error("Static method '" + actualStructType + "::" + staticCall->methodName + "' not found (and no inherited method found)");
    }
    
    LLVMValueRef function = it->second;
    
    if (verbose_)
        printf("[codegen] genExpr: generating static method call to %s with %zu args\n", mangledName.c_str(), staticCall->args.size());
    
        std::vector<LLVMValueRef> args;
    
            bool isConstructor = false;
    
        if (functionTypes_.find(mangledName) != functionTypes_.end()) {
        TypeInfo returnType = functionTypes_[mangledName];
        if (returnType.type == QuarkType::Struct && returnType.structName == targetStructType) {
            isConstructor = true;
            if (verbose_)
                printf("[codegen] genStaticCall: detected constructor method '%s'\n", mangledName.c_str());
        }
    }
    
        if (!isConstructor) {
                LLVMValueRef selfPtr = nullptr;
        LLVMTypeRef structTy = nullptr;
        if (g_struct_types_) {
            auto itTy = g_struct_types_->find(actualStructType);
            if (itTy != g_struct_types_->end()) structTy = itTy->second;
        }
        if (!structTy) throw std::runtime_error("unknown struct type '" + actualStructType + "' for static call");

                auto localVarIt = g_named_values_->find(staticCall->structName);
        auto localTypeIt = g_named_types_->find(staticCall->structName);
        if (localVarIt != g_named_values_->end() && localTypeIt != g_named_types_->end()) {
                        LLVMTypeRef desiredPtrTy = LLVMPointerType(structTy, 0);
            selfPtr = LLVMBuildBitCast(builder_, localVarIt->second, desiredPtrTy, "self.ptr");
        }
        if (!selfPtr) {
            throw std::runtime_error("Static call requires a struct instance variable name (not type) to provide 'self'");
        }
        args.push_back(selfPtr);
        
        LLVMValueRef dynName = nullptr;
        if (staticCall->structName == "this") {
            auto dynIt = g_named_values_->find("__dyn_type_name");
            if (dynIt != g_named_values_->end()) {
                dynName = LLVMBuildLoad2(builder_, int8ptr_t_, dynIt->second, "dyn_type_name.load");
            }
        }
        if (!dynName) {
            dynName = LLVMBuildGlobalStringPtr(builder_, actualStructType.c_str(), "dyn_type_name");
        }
        args.push_back(dynName);
    }
    
        auto funcTypesIt = g_function_param_types_->find(mangledName);
    if (funcTypesIt != g_function_param_types_->end()) {
        const std::vector<LLVMTypeRef>& expectedTypes = funcTypesIt->second;
        
                for (size_t i = 0; i < staticCall->args.size(); ++i) {
            if (verbose_)
                printf("[codegen] genStaticCall: generating user argument %zu\n", i);
            
            LLVMValueRef argVal;
                                    size_t paramIndex = isConstructor ? i : (i + 2);
            if (paramIndex < expectedTypes.size()) {
                                if (isLLVMIntegerType(expectedTypes[paramIndex])) {
                    argVal = genExprIntWithType(staticCall->args[i].get(), expectedTypes[paramIndex]);
                } else if (expectedTypes[paramIndex] == bool_t_) {
                    argVal = genExprBool(staticCall->args[i].get());
                } else {
                    // String or other types
                    argVal = genExpr(staticCall->args[i].get());
                }
            } else {
                                argVal = genExpr(staticCall->args[i].get());
            }
            args.push_back(argVal);
            
            if (verbose_)
                printf("[codegen] genStaticCall: generated user argument %zu\n", i);
        }
    } else {
                for (size_t i = 0; i < staticCall->args.size(); ++i) {
            if (verbose_)
                printf("[codegen] genStaticCall: generating user argument %zu\n", i);
            
            LLVMValueRef arg = genExpr(staticCall->args[i].get());
            args.push_back(arg);
            
            if (verbose_)
                printf("[codegen] genStaticCall: generated user argument %zu\n", i);
        }
    }
    
    if (verbose_)
        printf("[codegen] genExpr: calling static method %s\n", mangledName.c_str());
    
    // Get the function type
    LLVMTypeRef functionType = LLVMGlobalGetValueType(function);
    
    if (verbose_)
        printf("[codegen] genExpr: got function type\n");
    
        LLVMTypeRef returnType = LLVMGetReturnType(functionType);
    bool isVoid = (LLVMGetTypeKind(returnType) == LLVMVoidTypeKind);
    
        // Call the function
    LLVMValueRef call = LLVMBuildCall2(builder_, functionType, function, args.data(), static_cast<unsigned int>(args.size()), isVoid ? "" : "call");
    
    if (verbose_)
        printf("[codegen] genExpr: built static method call\n");
    
    return call;
}

LLVMTypeRef ExpressionCodeGen::mapPointerType(const std::string& typeName) {
    auto trimWhitespace = [](std::string &s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
        if (start > 0) s.erase(0, start);
    };

    std::string work = typeName;
    trimWhitespace(work);

    if (work.empty()) {
        work = "void*";
    }

    size_t stars = 0;
    while (!work.empty() && work.back() == '*') {
        work.pop_back();
        ++stars;
    }

    trimWhitespace(work);
    if (stars == 0) {
        stars = 1;     }
    if (work.empty()) {
        work = "void";
    }

    LLVMTypeRef baseTy = nullptr;
    if (work == "void" || work == "char" || work == "str") {
        baseTy = LLVMInt8TypeInContext(ctx_);
    } else if (work == "int") {
        baseTy = int32_t_;
    } else if (work == "float") {
        baseTy = float_t_;
    } else if (work == "double") {
        baseTy = double_t_;
    } else if (work == "bool") {
        baseTy = bool_t_;
    } else if (g_struct_types_) {
        auto sit = g_struct_types_->find(work);
        if (sit != g_struct_types_->end()) {
            baseTy = sit->second;
        }
    }

    if (!baseTy) {
        baseTy = LLVMInt8TypeInContext(ctx_);
    }

    LLVMTypeRef ty = baseTy;
    for (size_t i = 0; i < stars; ++i) {
        ty = LLVMPointerType(ty, 0);
    }
    return ty;
}

LLVMTypeRef ExpressionCodeGen::quarkTypeToLLVMType(QuarkType type) {
    switch (type) {
        case QuarkType::Int: return int32_t_;
        case QuarkType::Float: return float_t_;
        case QuarkType::Double: return double_t_;
        case QuarkType::String: return int8ptr_t_;
        case QuarkType::Map: return int8ptr_t_;
        case QuarkType::Boolean: return bool_t_;
        case QuarkType::Void: return LLVMVoidTypeInContext(ctx_);
        case QuarkType::Pointer: return int8ptr_t_; // Generic pointer type
        case QuarkType::Array: return int8ptr_t_; // Array decays to pointer
        case QuarkType::Struct: return int8ptr_t_;         default: return nullptr;
    }
}

QuarkType ExpressionCodeGen::llvmTypeToQuarkType(LLVMTypeRef type) {
    if (type == int32_t_) return QuarkType::Int;
    if (type == float_t_) return QuarkType::Float;
    if (type == double_t_) return QuarkType::Double;
    if (type == int8ptr_t_) return QuarkType::String;
        if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
        LLVMTypeRef elem = LLVMGetElementType(type);
        if (elem == int8ptr_t_) return QuarkType::Array;
    }
    if (type == bool_t_) return QuarkType::Boolean;
    if (type == LLVMVoidTypeInContext(ctx_)) return QuarkType::Void;
    return QuarkType::Unknown;
}

LLVMTypeRef ExpressionCodeGen::stringToLLVMType(const std::string& typeName) {
    if (!typeName.empty() && typeName.find('*') != std::string::npos) {
        return mapPointerType(typeName);
    }
    QuarkType quarkType = IntegerTypeUtils::stringToQuarkType(typeName);
    return quarkTypeToLLVMType(quarkType);
}

bool ExpressionCodeGen::isIntegerQuarkType(QuarkType type) {
    return IntegerTypeUtils::isIntegerType(type);
}

bool ExpressionCodeGen::isSignedIntegerType(QuarkType type) {
    return IntegerTypeUtils::isSignedIntegerType(type);
}

unsigned ExpressionCodeGen::getIntegerBitWidth(QuarkType type) {
    return IntegerTypeUtils::getBitWidth(type);
}

LLVMValueRef ExpressionCodeGen::genAddressOf(AddressOfExpr *addrOf) {
    if (verbose_) printf("[codegen] generating address-of expression\n");
    
        if (auto *var = dynamic_cast<VariableExprAST *>(addrOf->operand.get())) {
                if (g_function_map_) {
            auto fit = g_function_map_->find(var->name);
            if (fit != g_function_map_->end()) {
                if (verbose_) printf("[codegen] returning address of function: %s\n", var->name.c_str());
                                return fit->second;
            }
        }

                if (!g_named_values_ || g_named_values_->find(var->name) == g_named_values_->end()) {
            throw std::runtime_error("Unknown variable or function '" + var->name + "' in address-of expression");
        }
        LLVMValueRef varAlloca = (*g_named_values_)[var->name];
        if (verbose_) printf("[codegen] returning address of variable: %s\n", var->name.c_str());
        return varAlloca;
    }
    
        throw std::runtime_error("Address-of operator currently only supports simple variables");
}

LLVMValueRef ExpressionCodeGen::genDereference(DereferenceExpr *deref) {
    if (verbose_) printf("[codegen] generating dereference expression\n");
    
        LLVMValueRef ptrValue = genExpr(deref->operand.get());

    // Ensure we have a pointer
    LLVMTypeRef ptrTy = LLVMTypeOf(ptrValue);
    if (LLVMGetTypeKind(ptrTy) != LLVMPointerTypeKind) {
                auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
            auto file = sm->getFile(deref->location.filename);
            if (file) {
                throw EnhancedCodeGenError("dereference of non-pointer value", deref->location, file->content, ErrorCodes::INVALID_OPERATION, 1);
            }
        }
        throw CodeGenError("dereference of non-pointer value", deref->location);
    }

        LLVMTypeRef elemTy = int32_t_;     try {
        TypeInfo opT = inferType(deref->operand.get());
        switch (opT.type) {
            case QuarkType::Pointer:
                if (!opT.pointerTypeName.empty()) {
                    auto trimWhitespace = [](std::string &s) {
                        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
                        size_t start = 0;
                        while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) ++start;
                        if (start > 0) s.erase(0, start);
                    };

                    std::string pointee = opT.pointerTypeName;
                    trimWhitespace(pointee);
                    if (!pointee.empty() && pointee.back() == '*') {
                        pointee.pop_back();
                        trimWhitespace(pointee);
                    }

                    if (!pointee.empty()) {
                        if (pointee.find('*') != std::string::npos) {
                            elemTy = mapPointerType(pointee);
                        } else {
                            QuarkType baseQt = IntegerTypeUtils::stringToQuarkType(pointee);
                            if (baseQt == QuarkType::Pointer) {
                                elemTy = mapPointerType(pointee);
                            } else if (baseQt != QuarkType::Unknown && baseQt != QuarkType::Struct) {
                                LLVMTypeRef baseTy = quarkTypeToLLVMType(baseQt);
                                if (baseTy) elemTy = baseTy;
                            } else if (g_struct_types_) {
                                auto sit = g_struct_types_->find(pointee);
                                if (sit != g_struct_types_->end()) {
                                    elemTy = sit->second;
                                }
                            }
                        }
                    }
                }
                break;
            case QuarkType::String:
                                elemTy = int32_t_;
                break;
            case QuarkType::Array:
                                elemTy = quarkTypeToLLVMType(opT.elementType);
                if (!elemTy) elemTy = int32_t_;
                break;
            default:
                                if (opT.type == QuarkType::Int) elemTy = int32_t_;
                else if (opT.type == QuarkType::Float) elemTy = float_t_;
                else if (opT.type == QuarkType::Double) elemTy = double_t_;
                else if (opT.type == QuarkType::Boolean) elemTy = bool_t_;
                break;
        }
    } catch (...) {
            }

        LLVMTypeRef ptrElem = LLVMPointerType(int32_t_, 0);
        LLVMValueRef result = LLVMBuildLoad2(builder_, elemTy, ptrValue, "deref");
    if (verbose_) printf("[codegen] dereferenced pointer\n");
    return result;
}

LLVMValueRef ExpressionCodeGen::genRange(RangeExpr *range) {
    if (verbose_) printf("[codegen] generating range expression\n");
    
            LLVMValueRef endValue = genExpr(range->end.get());
    if (verbose_) printf("[codegen] generated range end value\n");
    return endValue;
}

bool ExpressionCodeGen::isLLVMIntegerType(LLVMTypeRef type) {
    return type == int32_t_;
}

LLVMValueRef ExpressionCodeGen::genExprIntWithType(ExprAST *expr, LLVMTypeRef targetType) {
        LLVMValueRef i32Val = genExprInt(expr);

        return i32Val;
}

LLVMValueRef ExpressionCodeGen::genArrayLiteral(ArrayLiteralExpr *arrayLiteral) {
    if (verbose_) printf("[codegen] generating array literal with %zu elements\n", arrayLiteral->elements.size());

    if (arrayLiteral->elements.empty()) {
        auto* er = errorReporter(); auto* sm = sourceManager(); if (er && sm) {
                        SourceLocation loc = arrayLiteral->location;
            auto file = sm->getFile(loc.filename);
            std::string src = file ? file->content : std::string();
            throw EnhancedCodeGenError("Cannot generate code for empty array literal", loc, src, ErrorCodes::INVALID_TYPE, 2);
        }
        throw CodeGenError("Cannot generate code for empty array literal", {});
    }

    // Determine array length
    unsigned length = (unsigned)arrayLiteral->elements.size();
    LLVMValueRef lengthVal = LLVMConstInt(LLVMInt32TypeInContext(ctx_), length, 0);

        TypeInfo firstElementType = inferType(arrayLiteral->elements[0].get());
    LLVMTypeRef elementType = quarkTypeToLLVMType(firstElementType.type);

        if (firstElementType.type == QuarkType::Struct && g_struct_types_) {
        auto it = g_struct_types_->find(firstElementType.structName);
        if (it != g_struct_types_->end()) {
            elementType = it->second;
        }
    }

        LLVMValueRef elemSize;
    if (elementType == int32_t_) {
        elemSize = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 4, 0);
    } else if (elementType == bool_t_) {
        elemSize = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 1, 0);
    } else {
                elemSize = LLVMConstInt(LLVMInt32TypeInContext(ctx_), (unsigned)sizeof(void*), 0);
    }

        LLVMValueRef dataBytes = LLVMBuildMul(builder_, lengthVal, elemSize, "arr_data_bytes");
    LLVMValueRef totalBytes = LLVMBuildAdd(builder_, dataBytes, LLVMConstInt(LLVMInt32TypeInContext(ctx_), 4, 0), "arr_total_bytes");

    // Call malloc(totalBytes)
    LLVMValueRef mallocFn = LLVMGetNamedFunction(module_, "malloc");
    if (!mallocFn) {
        // Fallback: declare if missing
        LLVMTypeRef mParams[] = { LLVMInt32TypeInContext(ctx_) };
        LLVMTypeRef mTy = LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(ctx_), 0), mParams, 1, 0);
        mallocFn = LLVMAddFunction(module_, "malloc", mTy);
    }
    LLVMTypeRef mallocTy = LLVMGlobalGetValueType(mallocFn);
    LLVMValueRef arrBuffer = LLVMBuildCall2(builder_, mallocTy, mallocFn, &totalBytes, 1, "arr_buf");

        LLVMValueRef hdrPtr = LLVMBuildPointerCast(builder_, arrBuffer, LLVMPointerType(LLVMInt32TypeInContext(ctx_), 0), "arr_hdr_ptr");
    LLVMBuildStore(builder_, lengthVal, hdrPtr);

        LLVMValueRef off4 = LLVMConstInt(LLVMInt32TypeInContext(ctx_), 4, 0);
    LLVMValueRef dataI8 = LLVMBuildGEP2(builder_, LLVMInt8TypeInContext(ctx_), arrBuffer, &off4, 1, "arr_data_i8");
    LLVMTypeRef dataPtrTy = LLVMPointerType(elementType, 0);
    LLVMValueRef dataPtr = LLVMBuildPointerCast(builder_, dataI8, dataPtrTy, "arr_data");

    // Initialize elements
    for (unsigned i = 0; i < length; ++i) {
        LLVMValueRef elemVal = genExpr(arrayLiteral->elements[i].get());
        LLVMValueRef idx = LLVMConstInt(LLVMInt32TypeInContext(ctx_), i, 0);
        LLVMValueRef elemPtr = LLVMBuildGEP2(builder_, elementType, dataPtr, &idx, 1, "arr_elem_ptr");
        LLVMBuildStore(builder_, elemVal, elemPtr);
    }

    if (verbose_) printf("[codegen] generated heap array with header\n");

        return dataPtr;
}

LLVMValueRef ExpressionCodeGen::genMapLiteral(MapLiteralExpr *mapLiteral) {
    if (verbose_) printf("[codegen] generating map literal with %zu pairs\n", mapLiteral->pairs.size());

    // Call quark_map_new() to create the map
    LLVMValueRef mapNewFn = LLVMGetNamedFunction(module_, "quark_map_new");
    if (!mapNewFn) {
        throw CodeGenError("quark_map_new function not found", mapLiteral->location);
    }
    
    LLVMTypeRef mapNewTy = LLVMGlobalGetValueType(mapNewFn);
    LLVMValueRef mapPtr = LLVMBuildCall2(builder_, mapNewTy, mapNewFn, nullptr, 0, "map_literal");
    
    // Get quark_map_set function
    LLVMValueRef mapSetFn = LLVMGetNamedFunction(module_, "quark_map_set");
    if (!mapSetFn) {
        throw CodeGenError("quark_map_set function not found", mapLiteral->location);
    }
    
    // For each key-value pair, call quark_map_set(map, key, value)
    for (const auto& pair : mapLiteral->pairs) {
        LLVMValueRef keyVal = genExpr(pair.first.get());
        LLVMValueRef valueVal = genExpr(pair.second.get());
        
        LLVMValueRef args[] = { mapPtr, keyVal, valueVal };
        LLVMTypeRef mapSetTy = LLVMGlobalGetValueType(mapSetFn);
        LLVMBuildCall2(builder_, mapSetTy, mapSetFn, args, 3, "");
    }
    
    if (verbose_) printf("[codegen] generated map literal\n");
    return mapPtr;
}

LLVMValueRef ExpressionCodeGen::genArrayAccess(ArrayAccessExpr *arrayAccess) {
    if (verbose_) printf("[codegen] generating array access\n");
    
    TypeInfo arrayTypeInfo = inferType(arrayAccess->array.get());
    
    // Handle map subscript access map["key"]
    if (arrayTypeInfo.type == QuarkType::Map) {
        if (verbose_) printf("[codegen] generating map subscript access\n");
        
        LLVMValueRef mapPtr = genExpr(arrayAccess->array.get());
        LLVMValueRef keyVal = genExpr(arrayAccess->index.get());
        
        // Call quark_map_get(map, key)
        LLVMValueRef mapGetFn = LLVMGetNamedFunction(module_, "quark_map_get");
        if (!mapGetFn) {
            throw CodeGenError("quark_map_get function not found", arrayAccess->location);
        }
        
        LLVMValueRef args[] = { mapPtr, keyVal };
        LLVMTypeRef mapGetTy = LLVMGlobalGetValueType(mapGetFn);
        LLVMValueRef result = LLVMBuildCall2(builder_, mapGetTy, mapGetFn, args, 2, "map_get_result");
        
        if (verbose_) printf("[codegen] generated map subscript access\n");
        return result;
    }
    
    // Handle array/string access
    LLVMValueRef arrayPtr = genExpr(arrayAccess->array.get());
    LLVMValueRef indexValue = genExprInt(arrayAccess->index.get());
    
    LLVMTypeRef elementType = int32_t_; // default
    
    if (arrayTypeInfo.type == QuarkType::Array) {
        elementType = quarkTypeToLLVMType(arrayTypeInfo.elementType);
    } else if (arrayTypeInfo.type == QuarkType::String) {
        elementType = int32_t_;
    }
    
    LLVMTypeRef desiredPtrTy = LLVMPointerType(elementType, 0);
    if (LLVMTypeOf(arrayPtr) != desiredPtrTy) {
        arrayPtr = LLVMBuildPointerCast(builder_, arrayPtr, desiredPtrTy, "arr_as_elem_ptr");
    }
    LLVMValueRef elementPtr = LLVMBuildGEP2(builder_, elementType, arrayPtr, &indexValue, 1, "array_access_ptr");
    
    LLVMValueRef result = LLVMBuildLoad2(builder_, elementType, elementPtr, "array_access_value");
    
    if (verbose_) printf("[codegen] generated array access\n");
    return result;
}
