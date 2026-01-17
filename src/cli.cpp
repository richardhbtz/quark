#include "../include/cli.h"
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#if defined(ERROR)
#undef ERROR
#endif
#if defined(WARNING)
#undef WARNING
#endif
#if defined(INFO)
#undef INFO
#endif
#else
#include <unistd.h>
#endif

CLI g_cli;

CLI::CLI() : outputLevel_(OutputLevel::NORMAL), colorEnabled_(true), spinnerActive_(false)
{
    detectColorSupport();
}

static bool detectUnicodeSupport()
{
#ifdef _WIN32
    UINT cp = GetConsoleOutputCP();
    return cp == 65001;
#else
    const char *lang = getenv("LANG");
    if (!lang)
        return true;
    std::string s(lang);
    for (auto &c : s)
        c = tolower(c);
    return s.find("utf-8") != std::string::npos || s.find("utf8") != std::string::npos;
#endif
}

void CLI::setOutputLevel(OutputLevel level)
{
    outputLevel_ = level;
}

void CLI::setColorEnabled(bool enabled)
{
    colorEnabled_ = enabled;
}

bool CLI::isColorEnabled() const
{
    return colorEnabled_;
}

void CLI::setEchoEnabled(bool enabled)
{
    echoEnabled_ = enabled;
}

bool CLI::isEchoEnabled() const
{
    return echoEnabled_;
}

void CLI::detectColorSupport()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
    {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
    colorEnabled_ = _isatty(_fileno(stdout));
#else
    colorEnabled_ = isatty(STDOUT_FILENO);
#endif
    unicodeEnabled_ = detectUnicodeSupport();
}

std::string CLI::colorize(const std::string &text, const std::string &color)
{
    if (!colorEnabled_)
        return text;
    return color + text + Colors::RESET;
}

std::string CLI::getTypeColor(MessageType type) const
{
    switch (type)
    {
    case MessageType::SUCCESS:
        return Colors::BRIGHT_GREEN;
    case MessageType::ERROR:
        return Colors::BRIGHT_RED;
    case MessageType::WARNING:
        return Colors::BRIGHT_YELLOW;
    case MessageType::INFO:
        return Colors::BRIGHT_BLUE;
    case MessageType::PROGRESS:
        return Colors::BRIGHT_CYAN;
    case MessageType::DEBUG:
        return Colors::GRAY;
    default:
        return Colors::RESET;
    }
}

std::string CLI::getTypePrefix(MessageType type) const
{
    if (!unicodeEnabled_)
    {
        switch (type)
        {
        case MessageType::SUCCESS:
            return "ok ";
        case MessageType::ERROR:
            return "err ";
        case MessageType::WARNING:
            return "warn ";
        case MessageType::INFO:
            return ". ";
        case MessageType::PROGRESS:
            return "> ";
        case MessageType::DEBUG:
            return "~ ";
        default:
            return "";
        }
    }

    switch (type)
    {
    case MessageType::SUCCESS:
        return Symbols::SUCCESS + " ";
    case MessageType::ERROR:
        return Symbols::ERROR + " ";
    case MessageType::WARNING:
        return Symbols::WARNING + " ";
    case MessageType::INFO:
        return Symbols::INFO + " ";
    case MessageType::PROGRESS:
        return Symbols::PROGRESS + " ";
    case MessageType::DEBUG:
        return Symbols::DEBUG + " ";
    default:
        return "";
    }
}

bool CLI::shouldPrint(MessageType type) const
{
    switch (type)
    {
    case MessageType::ERROR:
        return true;
    case MessageType::WARNING:
        return outputLevel_ >= OutputLevel::NORMAL;
    case MessageType::SUCCESS:
    case MessageType::INFO:
    case MessageType::PROGRESS:
        return outputLevel_ >= OutputLevel::NORMAL;
    case MessageType::DEBUG:
        return outputLevel_ >= OutputLevel::DEBUG;
    default:
        return outputLevel_ >= OutputLevel::NORMAL;
    }
}

std::string CLI::formatMessage(const std::string &message, MessageType type)
{
    if (!shouldPrint(type))
        return "";

    std::string prefix = getTypePrefix(type);
    std::string color = getTypeColor(type);

    if (colorEnabled_)
    {
        return colorize(prefix, color) + message;
    }
    else
    {
        std::string textPrefix;
        switch (type)
        {
        case MessageType::SUCCESS:
            textPrefix = "ok ";
            break;
        case MessageType::ERROR:
            textPrefix = "err ";
            break;
        case MessageType::WARNING:
            textPrefix = "warn ";
            break;
        case MessageType::INFO:
            textPrefix = ". ";
            break;
        case MessageType::PROGRESS:
            textPrefix = "> ";
            break;
        case MessageType::DEBUG:
            textPrefix = "~ ";
            break;
        default:
            textPrefix = "";
            break;
        }
        return textPrefix + message;
    }
}

void CLI::print(const std::string &message, MessageType type)
{
    std::string formatted = formatMessage(message, type);
    if (!formatted.empty())
    {
        if (messageHandler_)
            messageHandler_(type, message, false);
        if (echoEnabled_)
            std::cout << formatted << std::flush;
    }
}

void CLI::println(const std::string &message, MessageType type)
{
    std::string formatted = formatMessage(message, type);
    if (!formatted.empty())
    {
        if (messageHandler_)
            messageHandler_(type, message, true);
        if (echoEnabled_)
            std::cout << formatted << std::endl;
    }
}

void CLI::printRaw(const std::string &message)
{
    if (rawOutputHandler_)
        rawOutputHandler_(message, false);
    if (echoEnabled_)
        std::cout << message << std::flush;
}

void CLI::printlnRaw(const std::string &message)
{
    if (rawOutputHandler_)
        rawOutputHandler_(message, true);
    if (echoEnabled_)
        std::cout << message << std::endl;
}

void CLI::success(const std::string &message)
{
    println(message, MessageType::SUCCESS);
}

void CLI::error(const std::string &message)
{
    println(message, MessageType::ERROR);
}

void CLI::warning(const std::string &message)
{
    println(message, MessageType::WARNING);
}

void CLI::info(const std::string &message)
{
    println(message, MessageType::INFO);
}

void CLI::debug(const std::string &message)
{
    println(message, MessageType::DEBUG);
}

void CLI::progress(const std::string &message)
{
    println(message, MessageType::PROGRESS);
}

void CLI::clearCurrentLine()
{
    if (!echoEnabled_)
        return;
    if (colorEnabled_)
    {
        std::cout << "\r\033[K" << std::flush;
    }
    else
    {
        std::cout << "\r" << std::string(80, ' ') << "\r" << std::flush;
    }
}

void CLI::moveCursorUp(int lines)
{
    if (!echoEnabled_)
        return;
    if (colorEnabled_)
    {
        std::cout << "\033[" << lines << "A" << std::flush;
    }
}

void CLI::startSpinner(const std::string &message)
{
    if (!shouldPrint(MessageType::PROGRESS))
        return;

    spinnerActive_ = true;
    currentSpinnerMessage_ = message;
    spinnerStart_ = std::chrono::steady_clock::now();

    print(formatMessage(message + "...", MessageType::PROGRESS));
}

void CLI::updateSpinner(const std::string &message)
{
    if (!spinnerActive_ || !shouldPrint(MessageType::PROGRESS))
        return;

    clearCurrentLine();
    currentSpinnerMessage_ = message;
    print(formatMessage(message + "...", MessageType::PROGRESS));
}

void CLI::stopSpinner(bool success)
{
    if (!spinnerActive_)
        return;

    spinnerActive_ = false;
    if (echoEnabled_)
        clearCurrentLine();

    if (success)
    {
        println(currentSpinnerMessage_, MessageType::SUCCESS);
    }
    else
    {
        if (outputLevel_ != OutputLevel::QUIET)
        {
            println(currentSpinnerMessage_ + " failed", MessageType::ERROR);
        }
    }
}

void CLI::setMessageHandler(MessageHandler handler)
{
    messageHandler_ = std::move(handler);
}

void CLI::setRawOutputHandler(RawOutputHandler handler)
{
    rawOutputHandler_ = std::move(handler);
}

void CLI::startProgress(const std::string &message)
{
    progress(message);
}

void CLI::updateProgress(const std::string &message)
{
    progress(message);
}

void CLI::finishProgress(const std::string &message, bool success)
{
    if (success)
    {
        this->success(message);
    }
    else
    {
        error(message);
    }
}

void CLI::printVersion()
{
    printlnRaw(colorize("Quark", Colors::BOLD + Colors::BRIGHT_MAGENTA) + " " +
               colorize("0.1.0", Colors::BRIGHT_CYAN));
    printlnRaw(colorize("A modern systems programming language", Colors::GRAY));
}

void CLI::printUsage()
{
    printlnRaw(colorize("Usage:", Colors::BOLD) + " quark [OPTIONS] [FILE]");
    printlnRaw("");
    printlnRaw(colorize("Arguments:", Colors::BOLD));
    printlnRaw("  [FILE]    Input file to compile");
    printlnRaw("");
    printlnRaw(colorize("Options:", Colors::BOLD));
    printlnRaw("  -h, --help       Show this help message");
    printlnRaw("  -V, --version    Show version information");
    printlnRaw("  -v, --verbose    Enable verbose output");
    printlnRaw("  -q, --quiet      Suppress non-essential output");
    printlnRaw("  --debug          Enable debug output");
    printlnRaw("  -o <FILE>        Specify output file");
    printlnRaw("  --no-color       Disable colored output");
    printlnRaw("  --emit-llvm      Emit LLVM IR (default)");
    printlnRaw("  --emit-asm       Emit assembly code");
    printlnRaw("  -O, -O1          Enable basic optimizations (O1 level)");
    printlnRaw("  -O0              Disable optimizations");
    printlnRaw("  -O2              Enable standard optimizations (O2 level)");
    printlnRaw("  -O3              Enable aggressive optimizations (O3 level)");
    printlnRaw("  -L <DIR>         Add a library search path (alias: --libpath DIR)");
    printlnRaw("  -l <LIB>         Link against an additional library (repeatable)");
}

void CLI::printHelp()
{
    printVersion();
    printlnRaw("");
    printUsage();
    printlnRaw("");
    printlnRaw(colorize("Examples:", Colors::BOLD));
    printlnRaw("  quark main.k              Compile main.k to main");
    printlnRaw("  quark -o program main.k   Compile to program");
    printlnRaw("  quark -v main.k           Compile with verbose output");
    printlnRaw("  quark --emit-asm main.k   Emit assembly instead of LLVM IR");
    printlnRaw("");
    printlnRaw("  " + colorize("Add custom libraries:", Colors::BRIGHT_GREEN));
    printlnRaw("    quark -L libs -l sqlite main.k   # Add ./libs to search path and link sqlite");
    printlnRaw("");
    printlnRaw(colorize("Cache Options:", Colors::BOLD));
    printlnRaw("  --no-cache                Disable compilation cache");
    printlnRaw("  --clear-cache             Clear the compilation cache before compiling");
    printlnRaw("  --cache-dir <dir>         Set custom cache directory (default: .quark_cache)");
}

CLIArgs CLIArgs::parse(int argc, char **argv)
{
    CLIArgs args;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help")
        {
            args.showHelp = true;
        }
        else if (arg == "-V" || arg == "--version")
        {
            args.showVersion = true;
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            args.verbosity = OutputLevel::VERBOSE;
        }
        else if (arg == "-q" || arg == "--quiet")
        {
            args.verbosity = OutputLevel::QUIET;
        }
        else if (arg == "--debug")
        {
            args.verbosity = OutputLevel::DEBUG;
        }
        else if (arg == "--freestanding")
        {
            args.freeStanding = true;
        }
        else if (arg == "--no-color")
        {
            args.colorOutput = false;
        }
        else if (arg == "--emit-llvm")
        {
            args.emitLLVM = true;
            args.emitAssembly = false;
        }
        else if (arg == "--emit-asm")
        {
            args.emitAssembly = true;
            args.emitLLVM = false;
        }
        else if (arg == "-O" || arg == "-O1")
        {
            args.optimize = true;
            args.optimizationLevel = 1;
        }
        else if (arg == "-O0")
        {
            args.optimize = false;
            args.optimizationLevel = 0;
        }
        else if (arg == "-O2")
        {
            args.optimize = true;
            args.optimizationLevel = 2;
        }
        else if (arg == "-O3")
        {
            args.optimize = true;
            args.optimizationLevel = 3;
        }
        else if (arg == "-o")
        {
            if (i + 1 < argc)
            {
                args.outputFile = argv[++i];
            }
            else
            {
                g_cli.error("Option -o requires an argument");
                args.showHelp = true;
            }
        }
        else if (arg == "--libpath" || arg == "-L")
        {
            if (i + 1 < argc)
            {
                args.libraryPaths.push_back(argv[++i]);
            }
            else
            {
                g_cli.error("Option --libpath requires an argument");
                args.showHelp = true;
            }
        }
        else if (arg.rfind("-L", 0) == 0 && arg.size() > 2)
        {
            args.libraryPaths.push_back(arg.substr(2));
        }
        else if (arg == "--link-lib" || arg == "-l")
        {
            if (i + 1 < argc)
            {
                args.linkLibraries.push_back(argv[++i]);
            }
            else
            {
                g_cli.error("Option --link-lib requires an argument");
                args.showHelp = true;
            }
        }
        else if (arg.rfind("-l", 0) == 0 && arg.size() > 2)
        {
            args.linkLibraries.push_back(arg.substr(2));
        }
        else if (arg == "--no-cache")
        {
            args.useCache = false;
        }
        else if (arg == "--clear-cache")
        {
            args.clearCache = true;
        }
        else if (arg == "--cache-dir")
        {
            if (i + 1 < argc)
            {
                args.cacheDir = argv[++i];
            }
            else
            {
                g_cli.error("Option --cache-dir requires an argument");
                args.showHelp = true;
            }
        }
        else if (arg.find(".k") != std::string::npos)
        {
            args.inputFile = arg;
        }
        else if (arg[0] == '-')
        {
            g_cli.error("Unknown option: " + arg);
            args.showHelp = true;
        }
        else
        {
            args.inputFile = arg;
        }
    }

    return args;
}

bool CLIArgs::validate(CLI &cli) const
{
    if (showHelp || showVersion)
    {
        return true;
    }

    if (inputFile.empty())
    {
        cli.error("No input file specified");
        return false;
    }

    if (outputFile.empty())
    {
        cli.error("No output file specified");
        return false;
    }

    return true;
}

void CLIArgs::printDetailedHelp(CLI &cli)
{
    cli.printHelp();
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Compilation Process:", Colors::BOLD));
    cli.printlnRaw("  1. " + cli.colorize("Lexical Analysis", Colors::CYAN) + " - Tokenize source code into meaningful symbols");
    cli.printlnRaw("  2. " + cli.colorize("Syntax Analysis", Colors::CYAN) + " - Parse tokens into Abstract Syntax Tree (AST)");
    cli.printlnRaw("  3. " + cli.colorize("Type Checking", Colors::CYAN) + " - Validate types and resolve symbols");
    cli.printlnRaw("  4. " + cli.colorize("Code Generation", Colors::CYAN) + " - Generate LLVM Intermediate Representation");
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Advanced Examples:", Colors::BOLD));
    cli.printlnRaw("  " + cli.colorize("Basic compilation:", Colors::BRIGHT_GREEN));
    cli.printlnRaw("    quark hello.k                    # Compile hello.k to hello");
    cli.printlnRaw("");
    cli.printlnRaw("  " + cli.colorize("Custom output:", Colors::BRIGHT_GREEN));
    cli.printlnRaw("    quark -o program.ll main.k       # Specify output file");
    cli.printlnRaw("");
    cli.printlnRaw("  " + cli.colorize("Verbose compilation:", Colors::BRIGHT_GREEN));
    cli.printlnRaw("    quark -v main.k                  # Show detailed compilation steps");
    cli.printlnRaw("");
    cli.printlnRaw("  " + cli.colorize("Debug mode:", Colors::BRIGHT_GREEN));
    cli.printlnRaw("    quark --debug main.k             # Show internal compiler information");
    cli.printlnRaw("");
    cli.printlnRaw("  " + cli.colorize("Quiet mode:", Colors::BRIGHT_GREEN));
    cli.printlnRaw("    quark -q main.k                  # Suppress non-essential output");
    cli.printlnRaw("");
    cli.printlnRaw("  " + cli.colorize("Link custom libraries:", Colors::BRIGHT_GREEN));
    cli.printlnRaw("    quark --libpath libs --link-lib sqlite main.k");
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Error Handling:", Colors::BOLD));
    cli.printlnRaw("  Quark provides " + cli.colorize("Rust-style error messages", Colors::BRIGHT_CYAN) + " with:");
    cli.printlnRaw("  • Source code context with line numbers");
    cli.printlnRaw("  • Precise error location indicators (^)");
    cli.printlnRaw("  • Helpful suggestions and fixes");
    cli.printlnRaw("  • Color-coded severity levels");
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Supported Files:", Colors::BOLD));
    cli.printlnRaw("  " + cli.colorize(".k", Colors::BRIGHT_MAGENTA) + "    Quark source files");
    cli.printlnRaw("  " + cli.colorize(".ll", Colors::BRIGHT_MAGENTA) + "   LLVM IR output files");
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Environment:", Colors::BOLD));
    cli.printlnRaw("  " + cli.colorize("NO_COLOR", Colors::CYAN) + "        Disable colored output");
    cli.printlnRaw("  " + cli.colorize("QUARK_VERBOSE", Colors::CYAN) + "    Enable verbose mode by default");
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Resources:", Colors::BOLD));
    cli.printlnRaw("  " + cli.colorize("Documentation:", Colors::BRIGHT_BLUE) + " https://github.com/richardhbtz/quark/docs");
    cli.printlnRaw("  " + cli.colorize("Source Code:", Colors::BRIGHT_BLUE) + "   https://github.com/richardhbtz/quark");
    cli.printlnRaw("  " + cli.colorize("Bug Reports:", Colors::BRIGHT_BLUE) + "   https://github.com/richardhbtz/quark/issues");
    cli.printlnRaw("");

    cli.printlnRaw(cli.colorize("Build Information:", Colors::BOLD));
    cli.printlnRaw("  Built with LLVM and modern C++20");
    cli.printlnRaw("  Supports Windows, macOS, and Linux");
}
