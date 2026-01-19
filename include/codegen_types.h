#pragma once
#include "parser.h"
#include <string>
#include <stdexcept>

enum class QuarkType {
    Unknown,
    Int,        // 32-bit signed integer (alias for I32)
    I8,         // 8-bit signed integer
    I16,        // 16-bit signed integer
    I32,        // 32-bit signed integer
    I64,        // 64-bit signed integer
    U8,         // 8-bit unsigned integer
    U16,        // 16-bit unsigned integer
    U32,        // 32-bit unsigned integer
    U64,        // 64-bit unsigned integer
    Float,      // 32-bit floating point
    Double,     // 64-bit floating point  
    Char,       // 8-bit character
    String,
    Map,        // Built-in hash map type
    List,       // Built-in dynamic list type
    Boolean,
    Void,
    Struct,
    Pointer,    // Pointer to other types
    Array,      // Array of other types
    Null,       // Null literal type
    FunctionPointer // Function pointer type: fn(args) -> ret
};

// Represents a function pointer type signature
struct FunctionPointerTypeInfo {
    std::string returnType;                    // Return type as string
    std::vector<std::string> paramTypes;       // Parameter types as strings
    
    // Generate a canonical string representation
    std::string toString() const {
        std::string result = "fn(";
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (i > 0) result += ", ";
            result += paramTypes[i];
        }
        result += ") -> " + returnType;
        return result;
    }
    
    bool operator==(const FunctionPointerTypeInfo& other) const {
        return returnType == other.returnType && paramTypes == other.paramTypes;
    }
};

struct TypeInfo {
    QuarkType type;
    SourceLocation location;
    std::string structName;      // For struct types, the name of the struct
    QuarkType elementType;       // For array types, the type of elements
    size_t arraySize;            // For array types, the size of the array (0 = dynamic)
    std::string pointerTypeName; // For pointer types, the full pointer signature (e.g., "int*", "Foo**")
    std::shared_ptr<FunctionPointerTypeInfo> funcPtrInfo; // For function pointer types

    TypeInfo(QuarkType t = QuarkType::Unknown, SourceLocation loc = {}, std::string sName = "", 
             QuarkType elemType = QuarkType::Unknown, size_t arrSize = 0, std::string ptrType = "",
             std::shared_ptr<FunctionPointerTypeInfo> fpInfo = nullptr)
        : type(t), location(loc), structName(sName), elementType(elemType), arraySize(arrSize), 
          pointerTypeName(ptrType), funcPtrInfo(fpInfo) {}
};

// Utility functions for integer type handling
class IntegerTypeUtils {
public:
    // Check if a QuarkType is an integer type
    static bool isIntegerType(QuarkType type) {
        return type == QuarkType::Int ||
               type == QuarkType::I8 || type == QuarkType::I16 || 
               type == QuarkType::I32 || type == QuarkType::I64 ||
               type == QuarkType::U8 || type == QuarkType::U16 ||
               type == QuarkType::U32 || type == QuarkType::U64;
    }

    // Check if a QuarkType is a floating-point type
    static bool isFloatingType(QuarkType type) {
        return type == QuarkType::Float || type == QuarkType::Double;
    }

    // Check if a QuarkType is a numeric type (integer or floating-point)
    static bool isNumericType(QuarkType type) {
        return isIntegerType(type) || isFloatingType(type);
    }

    // Check if a QuarkType is a signed integer type
    static bool isSignedIntegerType(QuarkType type) {
        return type == QuarkType::Int ||
               type == QuarkType::I8 || type == QuarkType::I16 ||
               type == QuarkType::I32 || type == QuarkType::I64;
    }

    // Check if a QuarkType is an unsigned integer type
    static bool isUnsignedIntegerType(QuarkType type) {
        return type == QuarkType::U8 || type == QuarkType::U16 ||
               type == QuarkType::U32 || type == QuarkType::U64;
    }

    // Get the bit width of an integer type
    static unsigned getBitWidth(QuarkType type) {
        switch (type) {
            case QuarkType::Int:
            case QuarkType::I32:
            case QuarkType::U32:
                return 32;
            case QuarkType::I8:
            case QuarkType::U8:
                return 8;
            case QuarkType::I16:
            case QuarkType::U16:
                return 16;
            case QuarkType::I64:
            case QuarkType::U64:
                return 64;
            default:
                return 0;
        }
    }

    // Convert string type name to QuarkType
    static QuarkType stringToQuarkType(const std::string& typeName) {
        if (typeName == "int") return QuarkType::Int;
        if (typeName == "i8") return QuarkType::I8;
        if (typeName == "i16") return QuarkType::I16;
        if (typeName == "i32") return QuarkType::I32;
        if (typeName == "i64") return QuarkType::I64;
        if (typeName == "u8") return QuarkType::U8;
        if (typeName == "u16") return QuarkType::U16;
        if (typeName == "u32") return QuarkType::U32;
        if (typeName == "u64") return QuarkType::U64;
        if (typeName == "float") return QuarkType::Float;
        if (typeName == "double") return QuarkType::Double;
        if (typeName == "char") return QuarkType::Char;
        if (typeName == "str") return QuarkType::String;
        if (typeName == "map") return QuarkType::Map;
        if (typeName == "list") return QuarkType::List;
        if (typeName == "bool") return QuarkType::Boolean;
        if (typeName == "void") return QuarkType::Void;
        // Handle array types: int[], str[], etc.
        if (typeName.size() > 2 && typeName.substr(typeName.size() - 2) == "[]") {
            return QuarkType::Array;
        }
        // Handle pointer types: int*, Foo**, void*
        if (!typeName.empty() && typeName.find('*') != std::string::npos) {
            return QuarkType::Pointer;
        }
        return QuarkType::Unknown;
    }

    // Get element type from array type string
    static QuarkType getArrayElementType(const std::string& arrayTypeName) {
        if (arrayTypeName.size() > 2 && arrayTypeName.substr(arrayTypeName.size() - 2) == "[]") {
            std::string elementTypeName = arrayTypeName.substr(0, arrayTypeName.size() - 2);
            return stringToQuarkType(elementTypeName);
        }
        return QuarkType::Unknown;
    }

    // Convert QuarkType to string type name
    static std::string quarkTypeToString(QuarkType type) {
        switch (type) {
            case QuarkType::Int: return "int";
            case QuarkType::I8: return "i8";
            case QuarkType::I16: return "i16";
            case QuarkType::I32: return "i32";
            case QuarkType::I64: return "i64";
            case QuarkType::U8: return "u8";
            case QuarkType::U16: return "u16";
            case QuarkType::U32: return "u32";
            case QuarkType::U64: return "u64";
            case QuarkType::Float: return "float";
            case QuarkType::Double: return "double";
            case QuarkType::Char: return "char";
            case QuarkType::String: return "str";
            case QuarkType::Map: return "map";
            case QuarkType::List: return "list";
            case QuarkType::Boolean: return "bool";
            case QuarkType::Void: return "void";
            case QuarkType::Struct: return "struct";
            case QuarkType::Pointer: return "pointer";
            case QuarkType::Array: return "array";
            case QuarkType::FunctionPointer: return "fn";
            default: return "unknown";
        }
    }
    
    // Check if a type string represents a function pointer: fn(args) -> ret
    static bool isFunctionPointerType(const std::string& typeName) {
        return typeName.size() >= 2 && typeName.substr(0, 2) == "fn";
    }
};

class CodeGenError : public std::runtime_error
{
public:
    SourceLocation location;
    CodeGenError(const std::string &msg, const SourceLocation &loc)
        : std::runtime_error(formatError(msg, loc)), location(loc) {}
        
private:
    static std::string formatError(const std::string &msg, const SourceLocation &loc) {
        return loc.filename + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": codegen error: " + msg;
    }
};
