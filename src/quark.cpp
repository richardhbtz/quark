#include "../include/compiler_api.h"
#include "../include/cli.h"
#include "../include/package_manager.h"
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

void setupUTF8Console() {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

int main(int argc, char **argv)
{
    // Set up UTF-8 console support
    setupUTF8Console();

    std::filesystem::path exePath;
    try
    {
        exePath = std::filesystem::absolute(argv[0]);
    }
    catch (const std::exception &)
    {
        exePath.clear();
    }

    if (argc >= 2)
    {
        std::string_view firstArg{argv[1]};
        
                        bool isPackageCommand = false;
        if (!firstArg.empty() && firstArg[0] != '-')
        {
                        std::string firstArgLower(firstArg);
            std::transform(firstArgLower.begin(), firstArgLower.end(), firstArgLower.begin(), ::tolower);
            
            if (firstArgLower == "init" || firstArgLower == "build" || 
                firstArgLower == "run" || firstArgLower == "clean" ||
                firstArgLower == "add" || firstArgLower == "remove" ||
                firstArgLower == "update" || firstArgLower == "test" ||
                firstArgLower == "publish" || firstArgLower == "install" ||
                                firstArgLower == "package" || firstArgLower == "pkg" || firstArgLower == "pm")
            {
                isPackageCommand = true;
            }
        }
        
        if (isPackageCommand)
        {
            std::string firstArgStr(firstArg);
            std::transform(firstArgStr.begin(), firstArgStr.end(), firstArgStr.begin(), ::tolower);
            
                        if (firstArgStr == "package" || firstArgStr == "pkg" || firstArgStr == "pm")
            {
                int remaining = argc - 2;
                char** subArgs = argc > 2 ? argv + 2 : nullptr;
                return run_package_manager_cli(remaining > 0 ? remaining : 0, subArgs, exePath);
            }
            else
            {
                                int remaining = argc - 1;
                char** subArgs = argv + 1;
                return run_package_manager_cli(remaining, subArgs, exePath);
            }
        }
    }

    // Parse command line arguments
    CLIArgs args = CLIArgs::parse(argc, argv);

    try
    {
        if (!exePath.empty())
        {
            std::filesystem::path exeDir = exePath.parent_path();
            std::filesystem::path normalizedDir = exeDir.lexically_normal();
            std::string defaultLibPath = normalizedDir.string();

            bool alreadyPresent = std::any_of(args.libraryPaths.begin(), args.libraryPaths.end(),
                                             [&](const std::string &entry) {
                                                 std::filesystem::path entryPath(entry);
                                                 entryPath = entryPath.lexically_normal();
                                                 std::error_code ec;
                                                 if (std::filesystem::equivalent(entryPath, normalizedDir, ec))
                                                     return true;
                                                 return entryPath == normalizedDir;
                                             });

            if (!alreadyPresent)
                args.libraryPaths.push_back(defaultLibPath);
        }
    }
    catch (const std::exception &)
    {
            }

        g_cli.setOutputLevel(args.verbosity);
    g_cli.setColorEnabled(args.colorOutput);

        if (args.showHelp) {
        CLIArgs::printDetailedHelp(g_cli);
        return 0;
    }

    if (args.showVersion) {
        g_cli.printVersion();
        return 0;
    }

    // Validate arguments
    if (!args.validate(g_cli)) {
        g_cli.println("");
        g_cli.printUsage();
        return 1;
    }

    QuarkCompilerHandle* compiler = quark_compiler_create();
    if (!compiler) {
        g_cli.error("Failed to initialize the Quark compiler library");
        return 1;
    }

    quark_compiler_set_console_echo(compiler, 1);

    QuarkCompilerOptions options{};
    options.input_path = args.inputFile.c_str();
    options.output_path = args.outputFile.c_str();
    options.optimize = args.optimize ? 1 : 0;
    options.optimization_level = args.optimizationLevel;
    options.freestanding = args.freeStanding ? 1 : 0;
    options.emit_llvm = args.emitLLVM ? 1 : 0;
    options.emit_asm = args.emitAssembly ? 1 : 0;
    options.verbosity = static_cast<int>(args.verbosity);
    options.color_output = args.colorOutput ? 1 : 0;

    std::vector<const char *> libraryPathPtrs;
    libraryPathPtrs.reserve(args.libraryPaths.size());
    for (const auto &path : args.libraryPaths)
        libraryPathPtrs.push_back(path.c_str());
    options.library_paths = libraryPathPtrs.data();
    options.library_path_count = libraryPathPtrs.size();

    std::vector<const char *> linkLibPtrs;
    linkLibPtrs.reserve(args.linkLibraries.size());
    for (const auto &lib : args.linkLibraries)
        linkLibPtrs.push_back(lib.c_str());
    options.link_libraries = linkLibPtrs.data();
    options.link_library_count = linkLibPtrs.size();

    int status = quark_compiler_compile_file(compiler, &options);
    int exitCode = (status == QUARK_COMPILE_OK) ? 0 : 1;

    quark_compiler_destroy(compiler);
    return exitCode;
}
