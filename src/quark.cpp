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
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

// Helper function to run an executable and clean up afterward
static int runAndCleanup(const std::filesystem::path &executable)
{
    std::error_code ec;
    if (!std::filesystem::exists(executable, ec))
    {
        g_cli.error("Cannot run missing executable: " + executable.string());
        return 1;
    }

#ifdef _WIN32
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    int code = _wsystem(command.c_str());
#else
    std::string command = "\"" + executable.string() + "\"";
    int code = std::system(command.c_str());
#endif

    // Clean up the temporary executable
    std::filesystem::remove(executable, ec);

    if (code == -1)
    {
        g_cli.error("Failed to launch executable");
        return 1;
    }

    return code;
}

void setupUTF8Console()
{
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

    // Handle "quark run <file>" command - compile and run a file directly
    if (argc >= 3)
    {
        std::string_view firstArg{argv[1]};
        std::string firstArgLower(firstArg);
        std::transform(firstArgLower.begin(), firstArgLower.end(), firstArgLower.begin(), ::tolower);

        if (firstArgLower == "run" && std::string_view{argv[2]}.find(".k") != std::string_view::npos)
        {
            std::string inputFile = argv[2];

            // Set up CLI
            g_cli.setOutputLevel(OutputLevel::NORMAL);
            g_cli.setColorEnabled(true);

            // Check if input file exists
            std::error_code ec;
            if (!std::filesystem::exists(inputFile, ec))
            {
                g_cli.error("Source file not found: " + inputFile);
                return 1;
            }

            // Create temp output path
            std::filesystem::path inputPath(inputFile);
            std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
            if (ec)
                tempDir = ".";

#ifdef _WIN32
            std::filesystem::path outputPath = tempDir / (inputPath.stem().string() + "_quark_temp.exe");
#else
            std::filesystem::path outputPath = tempDir / (inputPath.stem().string() + "_quark_temp");
#endif

            // Create compiler
            QuarkCompilerHandle *compiler = quark_compiler_create();
            if (!compiler)
            {
                g_cli.error("Failed to initialize the Quark compiler library");
                return 1;
            }

            quark_compiler_set_console_echo(compiler, 1);

            // Set up options
            QuarkCompilerOptions options{};
            std::string inputPathStr = inputPath.string();
            std::string outputPathStr = outputPath.string();
            options.input_path = inputPathStr.c_str();
            options.output_path = outputPathStr.c_str();
            options.optimize = 0;
            options.optimization_level = 0;
            options.freestanding = 0;
            options.emit_llvm = 1;
            options.emit_asm = 0;
            options.verbosity = static_cast<int>(OutputLevel::NORMAL);
            options.color_output = 1;

            // Add exe directory to library paths
            std::vector<std::string> libraryPaths;
            if (!exePath.empty())
            {
                libraryPaths.push_back(exePath.parent_path().string());
            }

            std::vector<const char *> libraryPathPtrs;
            for (const auto &path : libraryPaths)
                libraryPathPtrs.push_back(path.c_str());
            options.library_paths = libraryPathPtrs.data();
            options.library_path_count = libraryPathPtrs.size();

            options.link_libraries = nullptr;
            options.link_library_count = 0;
            options.use_cache = 0;
            options.clear_cache = 0;
            options.cache_dir = "";

            // Compile
            int status = quark_compiler_compile_file(compiler, &options);
            quark_compiler_destroy(compiler);

            if (status != QUARK_COMPILE_OK)
            {
                return 1;
            }

            // Run and clean up
            g_cli.println("");
            return runAndCleanup(outputPath);
        }
    }

    if (argc >= 2)
    {
        std::string_view firstArg{argv[1]};

        bool isPackageCommand = false;
        if (!firstArg.empty() && firstArg[0] != '-')
        {
            std::string firstArgLower(firstArg);
            std::transform(firstArgLower.begin(), firstArgLower.end(), firstArgLower.begin(), ::tolower);

            // Check if second argument is a .k file - if so, use compiler not package manager
            bool hasKFileArg = false;
            if (argc >= 3)
            {
                std::string secondArg{argv[2]};
                if (secondArg.size() > 2 && secondArg.substr(secondArg.size() - 2) == ".k")
                {
                    hasKFileArg = true;
                }
            }

            // "build" and "run" with .k file go to compiler, not package manager
            if ((firstArgLower == "build" || firstArgLower == "run") && hasKFileArg)
            {
                isPackageCommand = false;
            }
            else if (firstArgLower == "init" || firstArgLower == "build" ||
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
                char **subArgs = argc > 2 ? argv + 2 : nullptr;
                return run_package_manager_cli(remaining > 0 ? remaining : 0, subArgs, exePath);
            }
            else
            {
                int remaining = argc - 1;
                char **subArgs = argv + 1;
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
                                              [&](const std::string &entry)
                                              {
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

    if (args.showHelp)
    {
        CLIArgs::printDetailedHelp(g_cli);
        return 0;
    }

    if (args.showVersion)
    {
        g_cli.printVersion();
        return 0;
    }

    // Validate arguments
    if (!args.validate(g_cli))
    {
        g_cli.println("");
        g_cli.printUsage();
        return 1;
    }

    QuarkCompilerHandle *compiler = quark_compiler_create();
    if (!compiler)
    {
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

    options.use_cache = args.useCache ? 1 : 0;
    options.clear_cache = args.clearCache ? 1 : 0;
    options.cache_dir = args.cacheDir.c_str();

    int status = quark_compiler_compile_file(compiler, &options);
    int exitCode = (status == QUARK_COMPILE_OK) ? 0 : 1;

    quark_compiler_destroy(compiler);
    return exitCode;
}
