#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// Manages source code files and provides context for error reporting
class SourceManager {
public:
    struct SourceFile {
        std::string filename;
        std::string content;
        std::vector<std::string> lines;
        std::vector<size_t> lineOffsets; // Byte offsets for each line start
        
        SourceFile(const std::string& name, const std::string& source);
        
        // Get line content by line number (1-based)
        std::string getLine(int lineNumber) const;
        
        // Get multiple lines around a center line
        std::vector<std::string> getLines(int centerLine, int contextLines = 2) const;
        
        // Get character position within a line
        int getColumnInLine(size_t absoluteOffset) const;
        
        // Get line number from absolute offset
        int getLineNumber(size_t absoluteOffset) const;
        
        // Convert line/column to absolute offset
        size_t getAbsoluteOffset(int line, int column) const;
        
        // Extract a snippet around a location
        std::string getSnippet(int line, int column, int length = 1) const;
    };
    
    SourceManager();
    
    // File management
    std::shared_ptr<SourceFile> addFile(const std::string& filename, const std::string& content);
    std::shared_ptr<SourceFile> getFile(const std::string& filename) const;
    bool hasFile(const std::string& filename) const;
    
    // Context extraction for error reporting
    struct ErrorContext {
        std::string filename;
        int line;
        int column;
        int length;
        std::string errorLine;
        std::vector<std::string> contextLines;
        std::vector<int> contextLineNumbers;
        std::string caretIndicator;
    };
    
    ErrorContext getErrorContext(const std::string& filename, int line, int column, 
                                int length = 1, int contextLines = 2) const;
    
    // Utility methods for source analysis
    std::string extractWord(const std::string& filename, int line, int column) const;
    std::vector<std::string> findSimilarIdentifiers(const std::string& filename, 
                                                   const std::string& target) const;
    
    // Source code formatting helpers
    std::string highlightRange(const std::string& line, int startCol, int endCol, 
                              const std::string& highlightColor) const;
    std::string createCaretLine(const std::string& sourceLine, int column, int length = 1) const;
    
private:
    std::unordered_map<std::string, std::shared_ptr<SourceFile>> files_;
    
    // Helper methods
    void splitIntoLines(const std::string& content, std::vector<std::string>& lines, 
                       std::vector<size_t>& lineOffsets) const;
    int calculateLevenshteinDistance(const std::string& a, const std::string& b) const;
    std::vector<std::string> extractIdentifiers(const std::string& content) const;
};

// Global source manager instance
extern std::unique_ptr<SourceManager> g_sourceManager;
