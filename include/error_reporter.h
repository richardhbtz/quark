#pragma once
#include "cli.h"
#include "lexer.h"
#include <string>
#include <vector>
#include <memory>

// Forward declaration
class FTXUIErrorDisplay;

// Enhanced error reporting system inspired by Rust's error messages
class ErrorReporter {
public:
    // Counters for summary reporting
    int errorCount_ = 0;
    int warningCount_ = 0;
    struct ErrorContext {
        std::string filename;
        std::string sourceCode;
        SourceLocation location;
        int length = 1; // span length for caret indicator
        std::string message;
        std::string errorCode;
        std::vector<std::string> suggestions;
        std::vector<std::string> notes;
        bool isWarning = false;
    };

    ErrorReporter(CLI& cli, bool useFTXUI = true);
    ~ErrorReporter();
    
    // Toggle FTXUI rendering
    void setUseFTXUI(bool enabled);
    bool isUsingFTXUI() const;
    
    // Report different types of errors
    void reportParseError(const std::string& message, const SourceLocation& location, 
                         const std::string& sourceCode, const std::string& errorCode = "", int length = 1);
    void reportCodeGenError(const std::string& message, const SourceLocation& location,
                           const std::string& sourceCode, const std::string& errorCode = "", int length = 1);
    void reportWarning(const std::string& message, const SourceLocation& location,
                      const std::string& sourceCode, const std::string& errorCode = "", int length = 1);
    
    // Add suggestions and notes to the last reported error
    void addSuggestion(const std::string& suggestion);
    void addNote(const std::string& note);
    
    // Format and display error with source context
    void displayError(const ErrorContext& context);
    // Print a final summary like Rust: "error: aborting due to N previous error(s)"
    void printSummary();
    
    // Get source lines around error location
    std::vector<std::string> getSourceLines(const std::string& sourceCode, 
                                           int centerLine, int contextLines = 2);
    
    // Format line numbers with proper padding
    std::string formatLineNumber(int lineNum, int maxLineNum);
    
    // Create caret indicator pointing to error location (handles tabs/CR for alignment)
    std::string createCaretIndicator(const std::string& originalLine, int column, int length = 1);

    // Normalize a source line for display (expand tabs, strip CR)
    static std::string normalizeForDisplay(const std::string& line, int tabWidth = 4);
    
    // Error code suggestions
    std::vector<std::string> getErrorSuggestions(const std::string& errorCode);
    
private:
    CLI& cli_;
    std::unique_ptr<ErrorContext> lastError_;
    std::unique_ptr<FTXUIErrorDisplay> ftxuiDisplay_;
    bool useFTXUI_ = true;
    
    // Helper methods
    // Formatting helpers (Rust-style, plain text)
    std::string formatErrorHeader(const ErrorContext& context);
    std::string formatLocation(const ErrorContext& context);
    std::string formatSourceBlock(const ErrorContext& context);
    std::string formatHelp(const std::vector<std::string>& suggestions);
    std::string formatNotes(const std::vector<std::string>& notes);
    int calculateMaxLineNumber(const std::vector<std::string>& lines, int centerLine);
};

// Enhanced error classes that use ErrorReporter
class EnhancedParseError : public std::runtime_error {
public:
    SourceLocation location;
    std::string sourceCode;
    std::string errorCode;
    int length = 1;
    
    EnhancedParseError(const std::string& message, const SourceLocation& loc, 
                      const std::string& source, const std::string& code = "", int spanLen = 1)
        : std::runtime_error(message), location(loc), sourceCode(source), errorCode(code), length(spanLen) {}
};

class EnhancedCodeGenError : public std::runtime_error {
public:
    SourceLocation location;
    std::string sourceCode;
    std::string errorCode;
    int length = 1;
    
    EnhancedCodeGenError(const std::string& message, const SourceLocation& loc,
                        const std::string& source, const std::string& code = "", int spanLen = 1)
        : std::runtime_error(message), location(loc), sourceCode(source), errorCode(code), length(spanLen) {}
};

// Common error codes
namespace ErrorCodes {
    const std::string UNEXPECTED_TOKEN = "E0001";
    const std::string MISSING_SEMICOLON = "E0002";
    const std::string UNDEFINED_VARIABLE = "E0003";
    const std::string TYPE_MISMATCH = "E0004";
    const std::string FUNCTION_NOT_FOUND = "E0005";
    const std::string INVALID_SYNTAX = "E0006";
    const std::string MISSING_BRACE = "E0007";
    const std::string DUPLICATE_DEFINITION = "E0008";
    const std::string INVALID_ASSIGNMENT = "E0009";
    const std::string MISSING_RETURN = "E0010";
    
    // Code generation errors
    const std::string CODEGEN_FAILED = "C0001";
    const std::string INVALID_TYPE = "C0002";
    const std::string LLVM_ERROR = "C0003";
    const std::string SYMBOL_NOT_FOUND = "C0004";
    const std::string INVALID_OPERATION = "C0005";
}

// Global error reporter instance
extern std::unique_ptr<ErrorReporter> g_errorReporter;
