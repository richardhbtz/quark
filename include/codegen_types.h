#pragma once
#include "parser.h"
#include <string>
#include <stdexcept>

enum class QuarkType {
    Unknown,
    Int,        // 32-bit signed integer (the only integer type)
    Float,      // 32-bit floating point
    Double,     // 64-bit floating point  
    Char,       // 8-bit character
    String,
    Map,        // String-keyed map (str -> str)
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
        return type == QuarkType::Int;
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
        return type == QuarkType::Int;
    }

    // Check if a QuarkType is an unsigned integer type
    static bool isUnsignedIntegerType(QuarkType type) {
        return false; // We only have signed int
    }

    // Get the bit width of an integer type
    static unsigned getBitWidth(QuarkType type) {
        switch (type) {
            case QuarkType::Int:
                return 32;
            default:
                return 0;
        }
    }

    // Convert string type name to QuarkType
    static QuarkType stringToQuarkType(const std::string& typeName) {
        if (typeName == "int") return QuarkType::Int;
        if (typeName == "float") return QuarkType::Float;
        if (typeName == "double") return QuarkType::Double;
        if (typeName == "char") return QuarkType::Char;
        if (typeName == "str") return QuarkType::String;
        if (typeName == "map") return QuarkType::Map;
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
            case QuarkType::Float: return "float";
            case QuarkType::Double: return "double";
            case QuarkType::Char: return "char";
            case QuarkType::String: return "str";
            case QuarkType::Map: return "map";
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
