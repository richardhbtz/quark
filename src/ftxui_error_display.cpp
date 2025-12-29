#include "../include/ftxui_error_display.h"
#include "../include/source_manager.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <sstream>
#include <regex>
#include <algorithm>

using namespace ftxui;

FTXUIErrorDisplay::FTXUIErrorDisplay(bool colorEnabled) : colorEnabled_(colorEnabled) {}

// Color scheme
Color FTXUIErrorDisplay::getErrorColor() const {
    return colorEnabled_ ? Color::Red : Color::White;
}

Color FTXUIErrorDisplay::getWarningColor() const {
    return colorEnabled_ ? Color::Yellow : Color::White;
}

Color FTXUIErrorDisplay::getInfoColor() const {
    return colorEnabled_ ? Color::Cyan : Color::White;
}

Color FTXUIErrorDisplay::getKeywordColor() const {
    return colorEnabled_ ? Color::Magenta : Color::White;
}

Color FTXUIErrorDisplay::getStringColor() const {
    return colorEnabled_ ? Color::Green : Color::White;
}

Color FTXUIErrorDisplay::getNumberColor() const {
    return colorEnabled_ ? Color::Cyan : Color::White;
}

Color FTXUIErrorDisplay::getCommentColor() const {
    return colorEnabled_ ? Color::GrayDark : Color::White;
}

Color FTXUIErrorDisplay::getIdentifierColor() const {
    return colorEnabled_ ? Color::White : Color::White;
}

Element FTXUIErrorDisplay::createErrorHeader(const ErrorReporter::ErrorContext& context) {
    auto typeColor = context.isWarning ? getWarningColor() : getErrorColor();
    auto typeText = context.isWarning ? "warning" : "error";
    
    Elements header;
    header.push_back(text(typeText) | bold | color(typeColor));
    
    if (!context.errorCode.empty()) {
        header.push_back(text("[" + context.errorCode + "]") | color(typeColor));
    }
    
        std::string msg = context.message;
    size_t p = msg.rfind("error: ");
    if (p != std::string::npos) {
        msg = msg.substr(p + std::string("error: ").size());
    }
    
    header.push_back(text(": " + msg));
    
    return hbox(std::move(header));
}

Element FTXUIErrorDisplay::createLocationInfo(const ErrorReporter::ErrorContext& context) {
    std::ostringstream oss;
    oss << "  --> " << context.filename << ":" << context.location.line << ":" << context.location.column;
    return text(oss.str()) | color(Color::CyanLight);
}

Element FTXUIErrorDisplay::highlightSourceLine(const std::string& line, bool isErrorLine) {
        std::vector<std::string> keywords = {
        "int", "str", "bool", "float", "double", "void",
        "if", "elif", "else", "while", "for", "in",
        "ret", "var", "struct", "data", "impl", "extend",
        "extern", "true", "false", "this", "match", "break", "continue"
    };
    
    Elements elements;
    std::string remaining = line;
    size_t pos = 0;
    
    while (pos < remaining.length()) {
        // Skip whitespace
        if (std::isspace(remaining[pos])) {
            size_t start = pos;
            while (pos < remaining.length() && std::isspace(remaining[pos])) pos++;
            elements.push_back(text(remaining.substr(start, pos - start)));
            continue;
        }
        
        // Check for comments
        if (pos + 1 < remaining.length() && remaining[pos] == '/' && remaining[pos + 1] == '/') {
            elements.push_back(text(remaining.substr(pos)) | color(getCommentColor()));
            break;
        }
        
        // Check for strings
        if (remaining[pos] == '"') {
            size_t end = pos + 1;
            while (end < remaining.length() && remaining[end] != '"') {
                if (remaining[end] == '\\' && end + 1 < remaining.length()) end++;
                end++;
            }
            if (end < remaining.length()) end++; // Include closing quote
            elements.push_back(text(remaining.substr(pos, end - pos)) | color(getStringColor()));
            pos = end;
            continue;
        }
        
        // Check for numbers
        if (std::isdigit(remaining[pos])) {
            size_t start = pos;
            while (pos < remaining.length() && (std::isdigit(remaining[pos]) || remaining[pos] == '.' || remaining[pos] == 'x')) pos++;
            elements.push_back(text(remaining.substr(start, pos - start)) | color(getNumberColor()));
            continue;
        }
        
                if (std::isalpha(remaining[pos]) || remaining[pos] == '_') {
            size_t start = pos;
            while (pos < remaining.length() && (std::isalnum(remaining[pos]) || remaining[pos] == '_')) pos++;
            std::string word = remaining.substr(start, pos - start);
            
            bool isKeyword = std::find(keywords.begin(), keywords.end(), word) != keywords.end();
            if (isKeyword) {
                elements.push_back(text(word) | color(getKeywordColor()) | bold);
            } else {
                elements.push_back(text(word) | color(getIdentifierColor()));
            }
            continue;
        }
        
                elements.push_back(text(std::string(1, remaining[pos])));
        pos++;
    }
    
    return hbox(std::move(elements));
}

Element FTXUIErrorDisplay::createCaretLine(const std::string& caretIndicator) {
    return text(caretIndicator) | color(getErrorColor()) | bold;
}

Element FTXUIErrorDisplay::createSourceBlock(const ErrorReporter::ErrorContext& context) {
    std::vector<std::string> lines;
    std::vector<int> lineNums;
    std::string errorLine;
    std::string caret;
    
        if (g_sourceManager && g_sourceManager->hasFile(context.filename)) {
        auto sm = g_sourceManager->getErrorContext(context.filename, context.location.line, 
                                                    context.location.column, context.length, 1);
        lines = sm.contextLines;
        lineNums = sm.contextLineNumbers;
        errorLine = sm.errorLine;
        caret = sm.caretIndicator;
    } else {
                std::istringstream stream(context.sourceCode);
        std::string line;
        std::vector<std::string> allLines;
        while (std::getline(stream, line)) {
            allLines.push_back(line);
        }
        
        int startLine = std::max(1, context.location.line - 1);
        int endLine = std::min((int)allLines.size(), context.location.line + 1);
        
        for (int i = startLine; i <= endLine; ++i) {
            if (i > 0 && i <= (int)allLines.size()) {
                lines.push_back(allLines[i - 1]);
                lineNums.push_back(i);
            }
        }
        
        if (!lines.empty()) {
            errorLine = lines.back();
            // Simple caret creation
            std::string normalized = ErrorReporter::normalizeForDisplay(errorLine);
            std::string indicator(normalized.length(), ' ');
            int col = std::max(0, std::min((int)normalized.length(), context.location.column - 1));
            for (int i = 0; i < context.length && col + i < (int)indicator.length(); ++i) {
                indicator[col + i] = '^';
            }
            caret = indicator;
        }
    }
    
    Elements sourceElements;
    
    // Find the error line index
    size_t errIdx = lines.empty() ? 0 : lines.size() - 1;
    for (size_t i = 0; i < lineNums.size(); ++i) {
        if (lineNums[i] == context.location.line) {
            errIdx = i;
            break;
        }
    }
    
    // Gutter
    sourceElements.push_back(text("   |") | color(Color::CyanLight));
    
        if (!lines.empty() && errIdx > 0) {
        std::ostringstream lineNumStr;
        lineNumStr << " " << lineNums[errIdx - 1] << " ";
        Elements lineEls;
        lineEls.push_back(text(lineNumStr.str()) | color(Color::CyanLight));
        lineEls.push_back(text("| "));
        lineEls.push_back(highlightSourceLine(ErrorReporter::normalizeForDisplay(lines[errIdx - 1])));
        sourceElements.push_back(hbox(std::move(lineEls)));
    }
    
        if (!lines.empty()) {
        std::ostringstream lineNumStr;
        lineNumStr << " " << lineNums[errIdx] << " ";
        Elements lineEls;
        lineEls.push_back(text(lineNumStr.str()) | color(Color::CyanLight));
        lineEls.push_back(text("| "));
        lineEls.push_back(highlightSourceLine(ErrorReporter::normalizeForDisplay(lines[errIdx]), true));
        sourceElements.push_back(hbox(std::move(lineEls)));
    }
    
    // Caret line
    if (!caret.empty()) {
        Elements caretEls;
        caretEls.push_back(text("   ") | color(Color::CyanLight));
        caretEls.push_back(text("| "));
        caretEls.push_back(createCaretLine(caret));
        sourceElements.push_back(hbox(std::move(caretEls)));
    } else {
        sourceElements.push_back(text("   |") | color(Color::CyanLight));
    }
    
    return vbox(std::move(sourceElements));
}

Element FTXUIErrorDisplay::createSuggestions(const std::vector<std::string>& suggestions) {
    if (suggestions.empty()) {
        return text("");
    }
    
    Elements suggestionElements;
    
        Elements firstLine;
    firstLine.push_back(text("help: ") | color(getInfoColor()) | bold);
    firstLine.push_back(text(suggestions[0]));
    suggestionElements.push_back(hbox(std::move(firstLine)));
    
    // Additional suggestions
    for (size_t i = 1; i < suggestions.size(); ++i) {
        suggestionElements.push_back(text("      " + suggestions[i]));
    }
    
    return vbox(std::move(suggestionElements));
}

Element FTXUIErrorDisplay::createNotes(const std::vector<std::string>& notes) {
    if (notes.empty()) {
        return text("");
    }
    
    Elements noteElements;
    for (const auto& note : notes) {
        Elements noteLine;
        noteLine.push_back(text("note: ") | color(Color::BlueLight) | bold);
        noteLine.push_back(text(note));
        noteElements.push_back(hbox(std::move(noteLine)));
    }
    
    return vbox(std::move(noteElements));
}

void FTXUIErrorDisplay::displayError(const ErrorReporter::ErrorContext& context) {
    Elements errorElements;
    
        errorElements.push_back(createErrorHeader(context));
    
    // Location info
    errorElements.push_back(createLocationInfo(context));
    
        errorElements.push_back(createSourceBlock(context));
    
    // Suggestions
    auto suggestions = createSuggestions(context.suggestions);
    if (suggestions) {
        errorElements.push_back(suggestions);
    }
    
    // Notes
    auto notes = createNotes(context.notes);
    if (notes) {
        errorElements.push_back(notes);
    }
    
    // Render to screen
    auto document = vbox(std::move(errorElements));
    auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
    Render(screen, document);
    screen.Print();
    std::cout << std::endl;
}

void FTXUIErrorDisplay::displaySummary(int errorCount, int warningCount) {
    if (errorCount == 0 && warningCount == 0) {
        return;
    }
    
    Elements summaryElements;
    
    if (errorCount > 0) {
        std::ostringstream oss;
        oss << "error: aborting due to " << errorCount << " previous error" << (errorCount == 1 ? "" : "s");
        summaryElements.push_back(text(oss.str()) | color(getErrorColor()) | bold);
    }
    
    if (warningCount > 0 && errorCount == 0) {
        std::ostringstream oss;
        oss << "warning: " << warningCount << " warning" << (warningCount == 1 ? "" : "s") << " emitted";
        summaryElements.push_back(text(oss.str()) | color(getWarningColor()) | bold);
    }
    
    if (!summaryElements.empty()) {
        Elements finalElements;
        finalElements.push_back(text("")); // Empty line before
        for (auto& elem : summaryElements) {
            finalElements.push_back(elem);
        }
        finalElements.push_back(text("")); // Empty line after
        
        auto document = vbox(std::move(finalElements));
        auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));
        Render(screen, document);
        std::cout << std::endl;
        screen.Print();
        std::cout << std::endl;
    }
}
