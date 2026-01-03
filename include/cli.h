#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <chrono>
#include <memory>
#include <functional>

// ANSI color codes for cross-platform colored output
namespace Colors
{
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string DIM = "\033[2m";

    // Text colors
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string GRAY = "\033[90m";

    // Bright colors
    const std::string BRIGHT_RED = "\033[91m";
    const std::string BRIGHT_GREEN = "\033[92m";
    const std::string BRIGHT_YELLOW = "\033[93m";
    const std::string BRIGHT_BLUE = "\033[94m";
    const std::string BRIGHT_MAGENTA = "\033[95m";
    const std::string BRIGHT_CYAN = "\033[96m";
    const std::string BRIGHT_WHITE = "\033[97m";
}

// Unicode indicators for different message types
namespace Symbols
{
    const std::string SUCCESS = "✓";
    const std::string ERROR = "✗";
    const std::string WARNING = "!";
    const std::string INFO = "·";
    const std::string PROGRESS = "›";
    const std::string DEBUG = "~";
    const std::string ARROW = "→";
    const std::string BULLET = "•";
}

enum class OutputLevel
{
    QUIET = 0,
    NORMAL = 1,
    VERBOSE = 2,
    DEBUG = 3
};

enum class MessageType
{
    SUCCESS,
    ERROR,
    WARNING,
    INFO,
    PROGRESS,
    DEBUG
};

class CLI
{
public:
    CLI();

    using MessageHandler = std::function<void(MessageType, const std::string&, bool)>;
    using RawOutputHandler = std::function<void(const std::string&, bool)>;

    // Output control
    void setOutputLevel(OutputLevel level);
    void setColorEnabled(bool enabled);
    bool isColorEnabled() const;
    void setEchoEnabled(bool enabled);
    bool isEchoEnabled() const;

    // Message formatting and output
    void print(const std::string &message, MessageType type = MessageType::INFO);
    void println(const std::string &message, MessageType type = MessageType::INFO);
    // Raw output without prefixes, emojis, or automatic coloring
    void printRaw(const std::string &message);
    void printlnRaw(const std::string &message);
    void success(const std::string &message);
    void error(const std::string &message);
    void warning(const std::string &message);
    void info(const std::string &message);
    void debug(const std::string &message);
    void progress(const std::string &message);

    // Formatted output with colors and emojis
    std::string colorize(const std::string &text, const std::string &color);
    std::string formatMessage(const std::string &message, MessageType type);

    // Progress indicators
    void startProgress(const std::string &message);
    void updateProgress(const std::string &message);
    void finishProgress(const std::string &message, bool success = true);

    // Spinner animation
    void startSpinner(const std::string &message);
    void updateSpinner(const std::string &message);
    void stopSpinner(bool success = true);

    // Output hooks for embedding scenarios
    void setMessageHandler(MessageHandler handler);
    void setRawOutputHandler(RawOutputHandler handler);

    // Help and usage
    void printHelp();
    void printVersion();
    void printUsage();

    // Diagnostic mode: suppress emojis/prefix for subsequent non-error messages
    void enterDiagnosticMode() { outputLevel_ = OutputLevel::QUIET; }
    void exitDiagnosticMode(OutputLevel restore = OutputLevel::NORMAL) { outputLevel_ = restore; }

private:
    OutputLevel outputLevel_;
    bool colorEnabled_;
    bool spinnerActive_;
    std::chrono::steady_clock::time_point spinnerStart_;
    std::string currentSpinnerMessage_;
    bool unicodeEnabled_ = false; // whether terminal likely supports UTF-8 / Unicode emojis
    bool echoEnabled_ = true;
    MessageHandler messageHandler_;
    RawOutputHandler rawOutputHandler_;

    // Helper methods
    bool shouldPrint(MessageType type) const;
    std::string getTypePrefix(MessageType type) const;
    std::string getTypeColor(MessageType type) const;
    void detectColorSupport();
    void clearCurrentLine();
    void moveCursorUp(int lines = 1);
};

// Command line argument parsing
struct CLIArgs
{
    std::string inputFile = "";

#ifdef _WIN32
    std::string outputFile = "main.exe";
#else
    std::string outputFile = "main";
#endif

    OutputLevel verbosity = OutputLevel::NORMAL;
    bool freeStanding = false;
    bool showHelp = false;
    bool showVersion = false;
    bool colorOutput = true;
    bool emitLLVM = true;
    bool emitAssembly = false;
    bool optimize = false;
    int optimizationLevel = 0; // 0 = no optimization, 1-3 = O1-O3
    std::vector<std::string> includePaths;
    std::vector<std::string> libraryPaths;
    std::vector<std::string> linkLibraries;
    
    bool useCache = true;
    bool clearCache = false;
    std::string cacheDir = ".quark_cache";

    // Parse command line arguments
    static CLIArgs parse(int argc, char **argv);

    // Validation
    bool validate(CLI &cli) const;

    // Help text
    static void printDetailedHelp(CLI &cli);
};

// Global CLI instance
extern CLI g_cli;
