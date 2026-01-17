#include "../include/source_manager.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>
#include <set>

std::unique_ptr<SourceManager> g_sourceManager;

SourceManager::SourceFile::SourceFile(const std::string &name, const std::string &source)
    : filename(name), content(source)
{
    size_t offset = 0;
    std::istringstream stream(content);
    std::string line;

    lineOffsets.push_back(0);
    while (std::getline(stream, line))
    {
        lines.push_back(line);
        offset += line.length() + 1;
        lineOffsets.push_back(offset);
    }
}

std::string SourceManager::SourceFile::getLine(int lineNumber) const
{
    if (lineNumber < 1 || lineNumber > (int)lines.size())
    {
        return "";
    }
    return lines[lineNumber - 1];
}

std::vector<std::string> SourceManager::SourceFile::getLines(int centerLine, int contextLines) const
{
    std::vector<std::string> result;

    int startLine = std::max(1, centerLine - contextLines);
    int endLine = std::min((int)lines.size(), centerLine + contextLines);

    for (int i = startLine; i <= endLine; ++i)
    {
        result.push_back(getLine(i));
    }

    return result;
}

int SourceManager::SourceFile::getLineNumber(size_t absoluteOffset) const
{
    auto it = std::upper_bound(lineOffsets.begin(), lineOffsets.end(), absoluteOffset);
    if (it == lineOffsets.begin())
    {
        return 1;
    }
    return static_cast<int>(std::distance(lineOffsets.begin(), it));
}

int SourceManager::SourceFile::getColumnInLine(size_t absoluteOffset) const
{
    int lineNum = getLineNumber(absoluteOffset);
    if (lineNum < 1 || lineNum > (int)lineOffsets.size())
    {
        return 1;
    }

    size_t lineStart = lineOffsets[lineNum - 1];
    return static_cast<int>(absoluteOffset - lineStart) + 1;
}

size_t SourceManager::SourceFile::getAbsoluteOffset(int line, int column) const
{
    if (line < 1 || line > (int)lineOffsets.size())
    {
        return 0;
    }

    size_t lineStart = lineOffsets[line - 1];
    return lineStart + std::max(0, column - 1);
}

std::string SourceManager::SourceFile::getSnippet(int line, int column, int length) const
{
    std::string sourceLine = getLine(line);
    if (sourceLine.empty() || column < 1)
    {
        return "";
    }

    int startCol = column - 1;
    int endCol = std::min((int)sourceLine.length(), startCol + length);

    if (startCol >= (int)sourceLine.length())
    {
        return "";
    }

    return sourceLine.substr(startCol, endCol - startCol);
}

SourceManager::SourceManager() {}

std::shared_ptr<SourceManager::SourceFile> SourceManager::addFile(const std::string &filename,
                                                                  const std::string &content)
{
    auto file = std::make_shared<SourceFile>(filename, content);
    files_[filename] = file;
    return file;
}

std::shared_ptr<SourceManager::SourceFile> SourceManager::getFile(const std::string &filename) const
{
    auto it = files_.find(filename);
    if (it != files_.end())
    {
        return it->second;
    }
    return nullptr;
}

bool SourceManager::hasFile(const std::string &filename) const
{
    return files_.find(filename) != files_.end();
}

SourceManager::ErrorContext SourceManager::getErrorContext(const std::string &filename,
                                                           int line, int column,
                                                           int length, int contextLines) const
{
    ErrorContext context;
    context.filename = filename;
    context.line = line;
    context.column = column;
    context.length = length;

    auto file = getFile(filename);
    if (!file)
    {
        return context;
    }

    context.errorLine = file->getLine(line);

    context.contextLines = file->getLines(line, contextLines);

    int startLine = std::max(1, line - contextLines);
    for (int i = 0; i < (int)context.contextLines.size(); ++i)
    {
        context.contextLineNumbers.push_back(startLine + i);
    }

    context.caretIndicator = createCaretLine(context.errorLine, column, length);

    return context;
}

std::string SourceManager::extractWord(const std::string &filename, int line, int column) const
{
    auto file = getFile(filename);
    if (!file)
    {
        return "";
    }

    std::string sourceLine = file->getLine(line);
    if (sourceLine.empty() || column < 1 || column > (int)sourceLine.length())
    {
        return "";
    }

    int start = column - 1;
    int end = start;

    while (start > 0 && (std::isalnum(sourceLine[start - 1]) || sourceLine[start - 1] == '_'))
    {
        start--;
    }

    while (end < (int)sourceLine.length() && (std::isalnum(sourceLine[end]) || sourceLine[end] == '_'))
    {
        end++;
    }

    if (start < end)
    {
        return sourceLine.substr(start, end - start);
    }

    return "";
}

std::vector<std::string> SourceManager::findSimilarIdentifiers(const std::string &filename,
                                                               const std::string &target) const
{
    auto file = getFile(filename);
    if (!file)
    {
        return {};
    }

    std::vector<std::string> identifiers = extractIdentifiers(file->content);
    std::vector<std::pair<std::string, int>> candidates;

    for (const auto &id : identifiers)
    {
        if (id != target)
        {
            int distance = calculateLevenshteinDistance(target, id);
            if (distance <= 3 && distance < (int)target.length())
            {
                candidates.emplace_back(id, distance);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b)
              { return a.second < b.second; });

    std::vector<std::string> suggestions;
    for (size_t i = 0; i < std::min(size_t(3), candidates.size()); ++i)
    {
        suggestions.push_back(candidates[i].first);
    }

    return suggestions;
}

std::string SourceManager::highlightRange(const std::string &line, int startCol, int endCol,
                                          const std::string &highlightColor) const
{
    if (startCol < 1 || startCol > (int)line.length())
    {
        return line;
    }

    int start = startCol - 1;
    int end = std::min((int)line.length(), endCol);

    std::string before = line.substr(0, start);
    std::string highlighted = line.substr(start, end - start);
    std::string after = line.substr(end);

    return before + highlightColor + highlighted + "\033[0m" + after;
}

std::string SourceManager::createCaretLine(const std::string &sourceLine, int column, int length) const
{
    const int tabWidth = 4;
    std::string indicator;
    indicator.reserve(sourceLine.size());

    int visualStart = 0;
    int logicalIndex = std::max(0, column - 1);
    int processed = 0;
    for (size_t i = 0; i < sourceLine.size() && processed < logicalIndex; ++i)
    {
        char ch = sourceLine[i];
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

    if (visualStart < 0)
        visualStart = 0;
    indicator.append(visualStart, ' ');
    int span = std::max(1, length);
    indicator.append(span, '^');
    return indicator;
}

void SourceManager::splitIntoLines(const std::string &content, std::vector<std::string> &lines,
                                   std::vector<size_t> &lineOffsets) const
{
    lines.clear();
    lineOffsets.clear();

    size_t offset = 0;
    std::istringstream stream(content);
    std::string line;

    lineOffsets.push_back(0);
    while (std::getline(stream, line))
    {
        lines.push_back(line);
        offset += line.length() + 1;
        lineOffsets.push_back(offset);
    }
}

int SourceManager::calculateLevenshteinDistance(const std::string &a, const std::string &b) const
{
    if (a.empty())
        return static_cast<int>(b.length());
    if (b.empty())
        return static_cast<int>(a.length());

    std::vector<std::vector<int>> dp(a.length() + 1, std::vector<int>(b.length() + 1));

    for (size_t i = 0; i <= a.length(); ++i)
    {
        dp[i][0] = static_cast<int>(i);
    }
    for (size_t j = 0; j <= b.length(); ++j)
    {
        dp[0][j] = static_cast<int>(j);
    }

    for (size_t i = 1; i <= a.length(); ++i)
    {
        for (size_t j = 1; j <= b.length(); ++j)
        {
            if (a[i - 1] == b[j - 1])
            {
                dp[i][j] = dp[i - 1][j - 1];
            }
            else
            {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }

    return dp[a.length()][b.length()];
}

std::vector<std::string> SourceManager::extractIdentifiers(const std::string &content) const
{
    std::vector<std::string> identifiers;
    std::regex identifierRegex(R"([a-zA-Z_][a-zA-Z0-9_]*)");
    std::sregex_iterator iter(content.begin(), content.end(), identifierRegex);
    std::sregex_iterator end;

    std::set<std::string> uniqueIdentifiers;

    while (iter != end)
    {
        std::string identifier = iter->str();
        if (identifier != "if" && identifier != "else" && identifier != "while" &&
            identifier != "for" && identifier != "return" && identifier != "int" &&
            identifier != "str" && identifier != "bool" && identifier != "void" &&
            identifier != "struct" && identifier != "fn" && identifier != "let" &&
            identifier != "const" && identifier != "true" && identifier != "false")
        {
            uniqueIdentifiers.insert(identifier);
        }
        ++iter;
    }

    identifiers.assign(uniqueIdentifiers.begin(), uniqueIdentifiers.end());
    return identifiers;
}
