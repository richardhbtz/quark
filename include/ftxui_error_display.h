#pragma once
#include "error_reporter.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>
#include <vector>

// FTXUI-based rich error display with syntax highlighting
class FTXUIErrorDisplay {
public:
    FTXUIErrorDisplay(bool colorEnabled = true);
    
    // Render an error context to the terminal using FTXUI
    void displayError(const ErrorReporter::ErrorContext& context);
    
    // Display a summary box at the end of compilation
    void displaySummary(int errorCount, int warningCount);
    
private:
    bool colorEnabled_;
    
    // Create FTXUI elements for different parts of the error display
    ftxui::Element createErrorHeader(const ErrorReporter::ErrorContext& context);
    ftxui::Element createLocationInfo(const ErrorReporter::ErrorContext& context);
    ftxui::Element createSourceBlock(const ErrorReporter::ErrorContext& context);
    ftxui::Element createSuggestions(const std::vector<std::string>& suggestions);
    ftxui::Element createNotes(const std::vector<std::string>& notes);
    
    // Syntax highlighting for source code
    ftxui::Element highlightSourceLine(const std::string& line, bool isErrorLine = false);
    ftxui::Element createCaretLine(const std::string& caretIndicator);
    
    // Color scheme
    ftxui::Color getErrorColor() const;
    ftxui::Color getWarningColor() const;
    ftxui::Color getInfoColor() const;
    ftxui::Color getKeywordColor() const;
    ftxui::Color getStringColor() const;
    ftxui::Color getNumberColor() const;
    ftxui::Color getCommentColor() const;
    ftxui::Color getIdentifierColor() const;
};
