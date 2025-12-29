#include "../include/compiler_api.h"

#include "../include/cli.h"
#include "../include/codegen.h"
#include "../include/error_reporter.h"
#include "../include/lexer.h"
#include "../include/parser.h"
#include "../include/source_manager.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct QuarkCompilerHandleImpl {
    QuarkDiagnosticCallback diagnosticCallback = nullptr;
    void* diagnosticUserData = nullptr;
    QuarkRawOutputCallback rawCallback = nullptr;
    void* rawUserData = nullptr;
    bool consoleEchoEnabled = true;
    int lastErrorCount = 0;
    int lastWarningCount = 0;
};

} // namespace

struct QuarkCompilerHandle {
    QuarkCompilerHandleImpl impl;
};

extern CLI g_cli;
extern std::unique_ptr<ErrorReporter> g_errorReporter;
extern std::unique_ptr<SourceManager> g_sourceManager;

namespace {

std::mutex g_compilerMutex;
QuarkCompilerHandle* g_activeHandle = nullptr;
std::mutex g_cliHelperMutex;
int g_cliLastErrors = 0;
int g_cliLastWarnings = 0;

static OutputLevel toOutputLevel(int verbosity) {
    switch (verbosity) {
        case QUARK_VERBOSITY_QUIET:
            return OutputLevel::QUIET;
        case QUARK_VERBOSITY_VERBOSE:
            return OutputLevel::VERBOSE;
        case QUARK_VERBOSITY_DEBUG:
            return OutputLevel::DEBUG;
        case QUARK_VERBOSITY_NORMAL:
        default:
            return OutputLevel::NORMAL;
    }
}

static QuarkLogLevel toLogLevel(MessageType type) {
    switch (type) {
        case MessageType::DEBUG:
            return QUARK_LOG_DEBUG;
        case MessageType::WARNING:
            return QUARK_LOG_WARNING;
        case MessageType::ERROR:
            return QUARK_LOG_ERROR;
        case MessageType::SUCCESS:
            return QUARK_LOG_SUCCESS;
        case MessageType::PROGRESS:
            return QUARK_LOG_PROGRESS;
        case MessageType::INFO:
        default:
            return QUARK_LOG_INFO;
    }
}

static std::string readFileToString(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static std::string defaultOutputPath() {
#ifdef _WIN32
    return "main.exe";
#else
    return "main";
#endif
}

static std::vector<std::string> copyStrings(const char* const* data, size_t count) {
    std::vector<std::string> result;
    if (!data || count == 0)
        return result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (data[i])
            result.emplace_back(data[i]);
    }
    return result;
}

static void attachHandlers(QuarkCompilerHandle* handle) {
    if (!handle) {
        g_cli.setMessageHandler(nullptr);
        g_cli.setRawOutputHandler(nullptr);
        g_cli.setEchoEnabled(true);
        g_activeHandle = nullptr;
        return;
    }

    if (handle->impl.diagnosticCallback) {
        auto* cb = handle->impl.diagnosticCallback;
        void* user = handle->impl.diagnosticUserData;
        g_cli.setMessageHandler([cb, user](MessageType type, const std::string& message, bool newline) {
            cb(toLogLevel(type), message.c_str(), newline ? 1 : 0, user);
        });
    } else {
        g_cli.setMessageHandler(nullptr);
    }

    if (handle->impl.rawCallback) {
        auto* cb = handle->impl.rawCallback;
        void* user = handle->impl.rawUserData;
        g_cli.setRawOutputHandler([cb, user](const std::string& text, bool newline) {
            cb(text.c_str(), newline ? 1 : 0, user);
        });
    } else {
        g_cli.setRawOutputHandler(nullptr);
    }

    g_cli.setEchoEnabled(handle->impl.consoleEchoEnabled);
    g_activeHandle = handle;
}

static void resetHandlers() {
    g_cli.setMessageHandler(nullptr);
    g_cli.setRawOutputHandler(nullptr);
    g_cli.setEchoEnabled(true);
    g_activeHandle = nullptr;
}

static int compileInternal(QuarkCompilerHandle* handle,
                           const std::string& source,
                           const std::string& logicalName,
                           const QuarkCompilerOptions& options,
                           const std::string& outputPath) {
    bool verbose = options.verbosity >= QUARK_VERBOSITY_VERBOSE;
    bool optimize = options.optimize != 0;
    int optimizationLevel = options.optimization_level;
    bool freestanding = options.freestanding != 0;
    auto additionalLibraries = copyStrings(options.link_libraries, options.link_library_count);
    auto additionalLibraryPaths = copyStrings(options.library_paths, options.library_path_count);

    g_sourceManager = std::make_unique<SourceManager>();
    g_errorReporter = std::make_unique<ErrorReporter>(g_cli);

    g_sourceManager->addFile(logicalName, source);

    try {
        bool showProgress = (options.verbosity >= QUARK_VERBOSITY_NORMAL) && g_cli.isColorEnabled();
        
                if (!showProgress) {
            g_cli.startSpinner("Compiling " + logicalName);
            g_cli.updateSpinner("Lexical analysis - tokenizing source code");
        }
        
        Lexer lexer(source, verbose, logicalName);

        if (!showProgress) {
            g_cli.updateSpinner("Syntax analysis - building AST");
        }
        Parser parser(lexer, verbose);
        auto ast = parser.parseProgram();

        if (!showProgress) {
            g_cli.updateSpinner("Code generation - emitting LLVM IR");
        }
        CodeGen codegen(verbose, optimize, optimizationLevel, freestanding,
            std::move(additionalLibraries), std::move(additionalLibraryPaths), showProgress);
        codegen.generate(ast.get(), outputPath);

        if (!showProgress) {
            g_cli.stopSpinner(true);
        }
        g_cli.success("Compilation completed successfully!");
        
                std::string displayPath = outputPath;
        try {
            std::filesystem::path absPath = std::filesystem::absolute(outputPath);
            std::filesystem::path currentPath = std::filesystem::current_path();
            displayPath = std::filesystem::relative(absPath, currentPath).string();
        } catch (...) {
                    }
        g_cli.info("Generated executable: " + displayPath);

        if (verbose) {
            g_cli.info("Compilation statistics:");
            g_cli.info("  • Input file: " + logicalName);
            g_cli.info("  • Output file: " + outputPath);
            if (auto file = g_sourceManager->getFile(logicalName)) {
                g_cli.info("  • Source lines: " + std::to_string(file->lines.size()));
            }
        }

        handle->impl.lastErrorCount = g_errorReporter ? g_errorReporter->errorCount_ : 0;
        handle->impl.lastWarningCount = g_errorReporter ? g_errorReporter->warningCount_ : 0;
        return QUARK_COMPILE_OK;
    } catch (const EnhancedParseError& e) {
        g_cli.enterDiagnosticMode();
        g_cli.stopSpinner(false);
        g_errorReporter->reportParseError(e.what(), e.location, e.sourceCode, e.errorCode, e.length);
        if (g_errorReporter)
            g_errorReporter->printSummary();
        handle->impl.lastErrorCount = g_errorReporter ? g_errorReporter->errorCount_ : 1;
        handle->impl.lastWarningCount = g_errorReporter ? g_errorReporter->warningCount_ : 0;
        g_cli.exitDiagnosticMode(toOutputLevel(options.verbosity));
        return QUARK_COMPILE_ERR_COMPILATION;
    } catch (const EnhancedCodeGenError& e) {
        g_cli.enterDiagnosticMode();
        g_cli.stopSpinner(false);
        g_errorReporter->reportCodeGenError(e.what(), e.location, e.sourceCode, e.errorCode, e.length);
        if (g_errorReporter)
            g_errorReporter->printSummary();
        handle->impl.lastErrorCount = g_errorReporter ? g_errorReporter->errorCount_ : 1;
        handle->impl.lastWarningCount = g_errorReporter ? g_errorReporter->warningCount_ : 0;
        g_cli.exitDiagnosticMode(toOutputLevel(options.verbosity));
        return QUARK_COMPILE_ERR_COMPILATION;
    } catch (const ParseError& e) {
        g_cli.enterDiagnosticMode();
        g_cli.stopSpinner(false);
        if (g_errorReporter && g_sourceManager) {
            std::string msg = e.what();
            size_t p = msg.rfind("error: ");
            if (p != std::string::npos)
                msg = msg.substr(p + std::string("error: ").size());
            auto file = g_sourceManager->getFile(logicalName);
            std::string sourceCode = file ? file->content : source;
            g_errorReporter->reportParseError(msg, e.location, sourceCode, ErrorCodes::INVALID_SYNTAX, 1);
            g_errorReporter->printSummary();
            handle->impl.lastErrorCount = g_errorReporter->errorCount_;
            handle->impl.lastWarningCount = g_errorReporter->warningCount_;
        } else {
            g_cli.error("Parse error: " + std::string(e.what()));
            handle->impl.lastErrorCount = 1;
            handle->impl.lastWarningCount = 0;
        }
        g_cli.exitDiagnosticMode(toOutputLevel(options.verbosity));
        return QUARK_COMPILE_ERR_COMPILATION;
    } catch (const CodeGenError& e) {
        g_cli.enterDiagnosticMode();
        g_cli.stopSpinner(false);
        if (g_errorReporter && g_sourceManager) {
            auto file = g_sourceManager->getFile(logicalName);
            std::string sourceCode = file ? file->content : source;
            g_errorReporter->reportCodeGenError(e.what(), e.location, sourceCode, ErrorCodes::CODEGEN_FAILED, 1);
            g_errorReporter->printSummary();
            handle->impl.lastErrorCount = g_errorReporter->errorCount_;
            handle->impl.lastWarningCount = g_errorReporter->warningCount_;
        } else {
            g_cli.error("Code generation error: " + std::string(e.what()));
            handle->impl.lastErrorCount = 1;
            handle->impl.lastWarningCount = 0;
        }
        g_cli.exitDiagnosticMode(toOutputLevel(options.verbosity));
        return QUARK_COMPILE_ERR_COMPILATION;
    } catch (const std::exception& e) {
        g_cli.enterDiagnosticMode();
        g_cli.stopSpinner(false);
        if (g_errorReporter && g_sourceManager) {
            SourceLocation loc;
            loc.filename = logicalName;
            loc.line = 1;
            loc.column = 1;
            auto file = g_sourceManager->getFile(logicalName);
            std::string src = file ? file->content : source;
            g_errorReporter->reportCodeGenError(std::string("internal compiler error: ") + e.what(),
                                                loc,
                                                src,
                                                ErrorCodes::LLVM_ERROR,
                                                1);
            g_errorReporter->printSummary();
            handle->impl.lastErrorCount = g_errorReporter->errorCount_;
            handle->impl.lastWarningCount = g_errorReporter->warningCount_;
        } else {
            g_cli.error("Internal compiler error: " + std::string(e.what()));
            handle->impl.lastErrorCount = 1;
            handle->impl.lastWarningCount = 0;
        }
        g_cli.exitDiagnosticMode(toOutputLevel(options.verbosity));
        return QUARK_COMPILE_ERR_INTERNAL;
    }
}

} // namespace

QuarkCompilerHandle* quark_compiler_create(void) {
    auto* handle = new QuarkCompilerHandle();
    handle->impl.consoleEchoEnabled = true;
    handle->impl.lastErrorCount = 0;
    handle->impl.lastWarningCount = 0;
    return handle;
}

void quark_compiler_destroy(QuarkCompilerHandle* handle) {
    if (!handle)
        return;
    if (g_activeHandle == handle)
        resetHandlers();
    delete handle;
}

void quark_compiler_set_diagnostic_callback(QuarkCompilerHandle* handle,
                                            QuarkDiagnosticCallback callback,
                                            void* user_data) {
    if (!handle)
        return;
    handle->impl.diagnosticCallback = callback;
    handle->impl.diagnosticUserData = user_data;
    if (g_activeHandle == handle)
        attachHandlers(handle);
}

void quark_compiler_set_raw_output_callback(QuarkCompilerHandle* handle,
                                            QuarkRawOutputCallback callback,
                                            void* user_data) {
    if (!handle)
        return;
    handle->impl.rawCallback = callback;
    handle->impl.rawUserData = user_data;
    if (g_activeHandle == handle)
        attachHandlers(handle);
}

void quark_compiler_set_console_echo(QuarkCompilerHandle* handle, int enabled) {
    if (!handle)
        return;
    handle->impl.consoleEchoEnabled = enabled != 0;
    if (g_activeHandle == handle)
        g_cli.setEchoEnabled(handle->impl.consoleEchoEnabled);
}

int quark_compiler_compile_file(QuarkCompilerHandle* handle,
                                const QuarkCompilerOptions* options) {
    if (!handle || !options || !options->input_path)
        return QUARK_COMPILE_ERR_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock(g_compilerMutex);

    handle->impl.lastErrorCount = 0;
    handle->impl.lastWarningCount = 0;

    attachHandlers(handle);

    g_cli.setOutputLevel(toOutputLevel(options->verbosity));
    g_cli.setColorEnabled(options->color_output != 0);

    std::filesystem::path inputPath(options->input_path);
    if (!std::filesystem::exists(inputPath)) {
        std::string missing = inputPath.string();
        g_cli.error("Input file not found: " + missing);
        g_cli.info("Make sure the file exists and the path is correct");
        handle->impl.lastErrorCount = 1;
        handle->impl.lastWarningCount = 0;
        return QUARK_COMPILE_ERR_IO;
    }

    std::string source = readFileToString(inputPath);
    if (source.empty()) {
        g_cli.error("Could not read file: " + inputPath.string());
        handle->impl.lastErrorCount = 1;
        handle->impl.lastWarningCount = 0;
        return QUARK_COMPILE_ERR_IO;
    }

    std::string logicalName = inputPath.string();
    std::string outputPath;
    if (options->output_path && options->output_path[0] != '\0') {
        outputPath = options->output_path;
    } else {
        outputPath = defaultOutputPath();
    }

    int result = compileInternal(handle, source, logicalName, *options, outputPath);
    return result;
}

int quark_compiler_compile_source(QuarkCompilerHandle* handle,
                                  const char* source_text,
                                  const char* virtual_filename,
                                  const QuarkCompilerOptions* options) {
    if (!handle || !options || !source_text)
        return QUARK_COMPILE_ERR_INVALID_ARGUMENT;

    std::lock_guard<std::mutex> lock(g_compilerMutex);

    handle->impl.lastErrorCount = 0;
    handle->impl.lastWarningCount = 0;

    attachHandlers(handle);

    g_cli.setOutputLevel(toOutputLevel(options->verbosity));
    g_cli.setColorEnabled(options->color_output != 0);

    std::string logicalName = virtual_filename && virtual_filename[0] != '\0'
                                   ? std::string(virtual_filename)
                                   : std::string("<memory>");

    std::string outputPath;
    if (options->output_path && options->output_path[0] != '\0') {
        outputPath = options->output_path;
    } else {
        outputPath = defaultOutputPath();
    }

    std::string source(source_text);

    int result = compileInternal(handle, source, logicalName, *options, outputPath);
    return result;
}

int quark_compiler_get_error_count(const QuarkCompilerHandle* handle) {
    if (!handle)
        return 0;
    return handle->impl.lastErrorCount;
}

int quark_compiler_get_warning_count(const QuarkCompilerHandle* handle) {
    if (!handle)
        return 0;
    return handle->impl.lastWarningCount;
}

int quark_cli_compile_file(const char* input_path,
                           const char* output_path,
                           int optimize,
                           int optimization_level,
                           int freestanding,
                           int emit_llvm,
                           int emit_asm,
                           int verbosity,
                           int color_output) {
    std::lock_guard<std::mutex> lock(g_cliHelperMutex);

    auto* handle = quark_compiler_create();
    if (!handle)
        return QUARK_COMPILE_ERR_INTERNAL;

    quark_compiler_set_console_echo(handle, 1);

    QuarkCompilerOptions options{};
    options.input_path = input_path;
    options.output_path = (output_path && output_path[0] != '\0') ? output_path : nullptr;
    options.optimize = optimize;
    options.optimization_level = optimization_level;
    options.freestanding = freestanding;
    options.emit_llvm = emit_llvm;
    options.emit_asm = emit_asm;
    options.verbosity = verbosity;
    options.color_output = color_output;
    options.library_paths = nullptr;
    options.library_path_count = 0;
    options.link_libraries = nullptr;
    options.link_library_count = 0;

    g_cliLastErrors = 0;
    g_cliLastWarnings = 0;

    int status = quark_compiler_compile_file(handle, &options);
    g_cliLastErrors = quark_compiler_get_error_count(handle);
    g_cliLastWarnings = quark_compiler_get_warning_count(handle);

    quark_compiler_destroy(handle);
    return status;
}

int quark_cli_last_error_count(void) {
    std::lock_guard<std::mutex> lock(g_cliHelperMutex);
    return g_cliLastErrors;
}

int quark_cli_last_warning_count(void) {
    std::lock_guard<std::mutex> lock(g_cliHelperMutex);
    return g_cliLastWarnings;
}

const char* quark_cli_default_output(void) {
#ifdef _WIN32
    return "main.exe";
#else
    return "main";
#endif
}
