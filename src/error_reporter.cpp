#include "../include/error_reporter.h"
#include "../include/ftxui_error_display.h"
#include "../include/source_manager.h"
#include <sstream>
#include <algorithm>
#include <iomanip>

std::unique_ptr<ErrorReporter> g_errorReporter;

ErrorReporter::ErrorReporter(CLI &cli, bool useFTXUI)
    : cli_(cli), useFTXUI_(useFTXUI)
{
    if (useFTXUI_)
    {
        ftxuiDisplay_ = std::make_unique<FTXUIErrorDisplay>(cli.isColorEnabled());
    }
}

ErrorReporter::~ErrorReporter() = default;

void ErrorReporter::setUseFTXUI(bool enabled)
{
    useFTXUI_ = enabled;
    if (enabled && !ftxuiDisplay_)
    {
        ftxuiDisplay_ = std::make_unique<FTXUIErrorDisplay>(cli_.isColorEnabled());
    }
}

bool ErrorReporter::isUsingFTXUI() const
{
    return useFTXUI_ && ftxuiDisplay_ != nullptr;
}

void ErrorReporter::reportParseError(const std::string &message, const SourceLocation &location,
                                     const std::string &sourceCode, const std::string &errorCode, int length)
{
    ErrorContext context;
    context.filename = location.filename;
    context.sourceCode = sourceCode;
    context.location = location;
    context.length = std::max(1, length);
    context.message = message;
    context.errorCode = errorCode;
    context.isWarning = false;
    context.suggestions = getErrorSuggestions(errorCode);

    lastError_ = std::make_unique<ErrorContext>(context);
    displayError(context);
}

void ErrorReporter::reportCodeGenError(const std::string &message, const SourceLocation &location,
                                       const std::string &sourceCode, const std::string &errorCode, int length)
{
    ErrorContext context;
    context.filename = location.filename;
    context.sourceCode = sourceCode;
    context.location = location;
    context.length = std::max(1, length);
    context.message = message;
    context.errorCode = errorCode;
    context.isWarning = false;
    context.suggestions = getErrorSuggestions(errorCode);

    lastError_ = std::make_unique<ErrorContext>(context);
    displayError(context);
}

void ErrorReporter::reportWarning(const std::string &message, const SourceLocation &location,
                                  const std::string &sourceCode, const std::string &errorCode, int length)
{
    ErrorContext context;
    context.filename = location.filename;
    context.sourceCode = sourceCode;
    context.location = location;
    context.length = std::max(1, length);
    context.message = message;
    context.errorCode = errorCode;
    context.isWarning = true;
    context.suggestions = getErrorSuggestions(errorCode);

    lastError_ = std::make_unique<ErrorContext>(context);
    displayError(context);
}

void ErrorReporter::addSuggestion(const std::string &suggestion)
{
    if (lastError_)
    {
        lastError_->suggestions.push_back(suggestion);
    }
}

void ErrorReporter::addNote(const std::string &note)
{
    if (lastError_)
    {
        lastError_->notes.push_back(note);
    }
}

std::vector<std::string> ErrorReporter::getSourceLines(const std::string &sourceCode,
                                                       int centerLine, int contextLines)
{
    std::vector<std::string> lines;
    std::istringstream stream(sourceCode);
    std::string line;

    while (std::getline(stream, line))
    {
        lines.push_back(line);
    }

    std::vector<std::string> result;
    int startLine = std::max(1, centerLine - contextLines);
    int endLine = std::min((int)lines.size(), centerLine + contextLines);

    for (int i = startLine; i <= endLine; ++i)
    {
        if (i > 0 && i <= (int)lines.size())
        {
            result.push_back(lines[i - 1]);
        }
    }

    return result;
}

std::string ErrorReporter::formatLineNumber(int lineNum, int maxLineNum)
{
    int width = std::to_string(maxLineNum).length();
    std::ostringstream oss;
    oss << std::setw(width) << lineNum;
    return oss.str();
}

std::string ErrorReporter::normalizeForDisplay(const std::string &line, int tabWidth)
{
    std::string out;
    out.reserve(line.size());
    int col = 0;
    for (char ch : line)
    {
        if (ch == '\r')
            continue;
        if (ch == '\t')
        {
            int spaces = tabWidth - (col % tabWidth);
            out.append(spaces, ' ');
            col += spaces;
        }
        else
        {
            out.push_back(ch);
            col++;
        }
    }
    return out;
}

std::string ErrorReporter::createCaretIndicator(const std::string &originalLine, int column, int length)
{
    const int tabWidth = 4;
    std::string line = normalizeForDisplay(originalLine, tabWidth);
    std::string indicator(line.length(), ' ');

    int visualStart = 0;
    int logicalIndex = std::max(0, column - 1);
    int processed = 0;
    for (size_t i = 0; i < originalLine.size() && processed < logicalIndex; ++i)
    {
        char ch = originalLine[i];
        if (ch == '\r')
            continue;
        if (ch == '\t')
        {
            int spaces = tabWidth - (visualStart % tabWidth);
            visualStart += spaces;
        }
        else
        {
            visualStart += 1;
        }
        processed += 1;
    }

    int startCol = std::max(0, std::min(visualStart, (int)line.length()));
    int endCol = std::max(startCol, std::min((int)line.length(), startCol + std::max(1, length)));

    for (int i = startCol; i < endCol && i < (int)indicator.length(); ++i)
    {
        indicator[i] = '^';
    }
    return indicator;
}

std::string ErrorReporter::formatErrorHeader(const ErrorContext &context)
{
    std::ostringstream oss;
    if (cli_.isColorEnabled())
        oss << (context.isWarning ? Colors::BRIGHT_YELLOW : Colors::BRIGHT_RED);
    oss << (context.isWarning ? "warning" : "error");
    if (!context.errorCode.empty())
        oss << "[" << context.errorCode << "]";
    std::string msg = context.message;
    size_t p = msg.rfind("error: ");
    if (p != std::string::npos)
        msg = msg.substr(p + std::string("error: ").size());
    oss << ": " << msg;
    if (cli_.isColorEnabled())
        oss << Colors::RESET;
    return oss.str();
}

std::string ErrorReporter::formatLocation(const ErrorContext &context)
{
    std::ostringstream oss;
    oss << "  --> " << context.filename << ":" << context.location.line << ":" << context.location.column;
    return oss.str();
}

std::string ErrorReporter::formatSourceBlock(const ErrorContext &context)
{
    std::ostringstream oss;

    std::vector<std::string> lines;
    std::vector<int> lineNums;
    std::string errorLine;
    std::string caret;

    if (g_sourceManager && g_sourceManager->hasFile(context.filename))
    {
        auto sm = g_sourceManager->getErrorContext(context.filename, context.location.line, context.location.column, context.length, 1);
        lines = sm.contextLines;
        lineNums = sm.contextLineNumbers;
        errorLine = sm.errorLine;
        caret = sm.caretIndicator;
    }
    else
    {

        auto sourceLines = getSourceLines(context.sourceCode, context.location.line, 1);
        if (sourceLines.empty())
            return std::string();
        int startLine = std::max(1, context.location.line - 1);
        if ((int)sourceLines.size() == 2)
        {
            lines.push_back(sourceLines[0]);
            lineNums.push_back(startLine);
        }
        lines.push_back(sourceLines.back());
        lineNums.push_back(context.location.line);
        errorLine = sourceLines.back();
        caret = createCaretIndicator(errorLine, context.location.column, context.length);
    }

    size_t errIdx = lines.empty() ? 0 : lines.size() - 1;
    for (size_t i = 0; i < lineNums.size(); ++i)
    {
        if (lineNums[i] == context.location.line)
        {
            errIdx = i;
            break;
        }
    }

    oss << "   |\n";
    if (!lines.empty() && errIdx > 0)
    {
        std::string prevLine = normalizeForDisplay(lines[errIdx - 1]);
        oss << " " << lineNums[errIdx - 1] << " | " << prevLine << "\n";
    }

    std::string displayLine = normalizeForDisplay(lines.empty() ? std::string() : lines[errIdx]);
    int displayLineNum = lineNums.empty() ? context.location.line : lineNums[errIdx];
    oss << " " << displayLineNum << " | " << displayLine << "\n";

    if (!caret.empty())
    {
        std::string colored;
        bool inRun = false;
        for (char c : caret)
        {
            if (c == '^')
            {
                if (!inRun)
                {
                    if (cli_.isColorEnabled())
                        colored += Colors::BRIGHT_RED;
                    inRun = true;
                }
                colored += c;
            }
            else
            {
                if (inRun)
                {
                    if (cli_.isColorEnabled())
                        colored += Colors::RESET;
                    inRun = false;
                }
                colored += c;
            }
        }
        if (inRun && cli_.isColorEnabled())
            colored += Colors::RESET;
        oss << "   | " << colored << "\n";
    }
    else
    {
        oss << "   |\n";
    }

    return oss.str();
}

std::string ErrorReporter::formatHelp(const std::vector<std::string> &suggestions)
{
    if (suggestions.empty())
        return std::string();
    std::ostringstream oss;
    if (cli_.isColorEnabled())
        oss << Colors::BRIGHT_CYAN;
    oss << "help: ";
    if (cli_.isColorEnabled())
        oss << Colors::RESET;
    oss << suggestions[0] << "\n";
    for (size_t i = 1; i < suggestions.size(); ++i)
        oss << "      " << suggestions[i] << "\n";
    return oss.str();
}

std::string ErrorReporter::formatNotes(const std::vector<std::string> &notes)
{
    if (notes.empty())
        return std::string();
    std::ostringstream oss;
    for (const auto &n : notes)
    {
        if (cli_.isColorEnabled())
            oss << Colors::BRIGHT_BLUE;
        oss << "note: ";
        if (cli_.isColorEnabled())
            oss << Colors::RESET;
        oss << n << "\n";
    }
    return oss.str();
}

void ErrorReporter::displayError(const ErrorContext &context)
{
    if (context.isWarning)
        ++warningCount_;
    else
        ++errorCount_;

    if (useFTXUI_ && ftxuiDisplay_)
    {
        ftxuiDisplay_->displayError(context);
        return;
    }

    cli_.printlnRaw(formatErrorHeader(context));
    cli_.printlnRaw(formatLocation(context));
    cli_.printRaw(formatSourceBlock(context));

    std::string help = formatHelp(context.suggestions);
    if (!help.empty())
        cli_.printRaw(help);
    std::string notes = formatNotes(context.notes);
    if (!notes.empty())
        cli_.printRaw(notes);
}

int ErrorReporter::calculateMaxLineNumber(const std::vector<std::string> &lines, int centerLine)
{
    return centerLine + (int)lines.size() / 2;
}

std::vector<std::string> ErrorReporter::getErrorSuggestions(const std::string &errorCode)
{
    std::vector<std::string> suggestions;

    if (errorCode == ErrorCodes::UNEXPECTED_TOKEN)
    {
        suggestions.push_back("Check for missing semicolons, braces, or parentheses");
        suggestions.push_back("Verify that all keywords are spelled correctly");
    }
    else if (errorCode == ErrorCodes::MISSING_SEMICOLON)
    {
        suggestions.push_back("Add a semicolon ';' at the end of the statement");
    }
    else if (errorCode == ErrorCodes::UNDEFINED_VARIABLE)
    {
        suggestions.push_back("Check if the variable is declared before use");
        suggestions.push_back("Verify the variable name spelling");
        suggestions.push_back("Ensure the variable is in scope");
    }
    else if (errorCode == ErrorCodes::TYPE_MISMATCH)
    {
        suggestions.push_back("Check that the types on both sides of the assignment match");
        suggestions.push_back("Consider using explicit type conversion");
    }
    else if (errorCode == ErrorCodes::FUNCTION_NOT_FOUND)
    {
        suggestions.push_back("Check if the function is declared before use");
        suggestions.push_back("Verify the function name spelling");
        suggestions.push_back("Ensure the function is imported if it's from another module");
    }
    else if (errorCode == ErrorCodes::INVALID_SYNTAX)
    {
        suggestions.push_back("Review the language syntax documentation");
        suggestions.push_back("Check for proper use of keywords and operators");
    }
    else if (errorCode == ErrorCodes::MISSING_BRACE)
    {
        suggestions.push_back("Add the missing opening or closing brace '{'  or '}'");
        suggestions.push_back("Check that all code blocks are properly enclosed");
    }
    else if (errorCode == ErrorCodes::DUPLICATE_DEFINITION)
    {
        suggestions.push_back("Remove the duplicate definition");
        suggestions.push_back("Use different names for different variables/functions");
    }
    else if (errorCode == ErrorCodes::INVALID_ASSIGNMENT)
    {
        suggestions.push_back("Check that you're assigning to a valid lvalue");
        suggestions.push_back("Ensure the variable is not declared as const");
    }
    else if (errorCode == ErrorCodes::MISSING_RETURN)
    {
        suggestions.push_back("Add a return statement to the function");
        suggestions.push_back("Ensure all code paths return a value");
    }
    else if (errorCode == ErrorCodes::CODEGEN_FAILED)
    {
        suggestions.push_back("Check for type compatibility issues");
        suggestions.push_back("Verify that all referenced symbols are defined");
    }
    else if (errorCode == ErrorCodes::INVALID_TYPE)
    {
        suggestions.push_back("Use a valid type name (int, str, bool, etc.)");
        suggestions.push_back("Check if custom types are properly defined");
    }
    else if (errorCode == ErrorCodes::LLVM_ERROR)
    {
        suggestions.push_back("This is an internal compiler error");
        suggestions.push_back("Please report this issue to the Quark developers");
    }
    else if (errorCode == ErrorCodes::SYMBOL_NOT_FOUND)
    {
        suggestions.push_back("Ensure the symbol is declared in the current scope");
        suggestions.push_back("Check for typos in the symbol name");
    }
    else if (errorCode == ErrorCodes::INVALID_OPERATION)
    {
        suggestions.push_back("Check that the operation is valid for the given types");
        suggestions.push_back("Verify operator precedence and associativity");
    }

    return suggestions;
}

void ErrorReporter::printSummary()
{
    if (useFTXUI_ && ftxuiDisplay_)
    {
        ftxuiDisplay_->displaySummary(errorCount_, warningCount_);
        return;
    }

    if (errorCount_ > 0)
    {
        std::ostringstream oss;
        if (cli_.isColorEnabled())
            oss << Colors::BRIGHT_RED;
        oss << "error";
        if (cli_.isColorEnabled())
            oss << Colors::RESET;
        oss << ": aborting due to " << errorCount_ << " previous error" << (errorCount_ == 1 ? "" : "s");
        cli_.printlnRaw("");
        cli_.printlnRaw(oss.str());
    }
}
