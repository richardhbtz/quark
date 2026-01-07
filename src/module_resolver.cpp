#include "../include/module_resolver.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>

std::unique_ptr<ModuleResolver> g_moduleResolver;

// Standard library modules that ship with the compiler
const std::vector<std::string> ModuleResolver::stdModules_ = {
    "json",
    "http",
    "ws",
    "toml",
    "io",
    "map"};

ModuleResolver::ModuleResolver(const std::filesystem::path &compilerPath,
                               const std::filesystem::path &projectPath)
    : compilerPath_(compilerPath), projectPath_(projectPath)
{
    // Normalize paths
    std::error_code ec;
    if (!compilerPath_.empty())
    {
        compilerPath_ = std::filesystem::absolute(compilerPath_, ec);
        // compilerPath should already be the directory containing lib/
    }
    if (!projectPath_.empty())
    {
        projectPath_ = std::filesystem::absolute(projectPath_, ec);
    }
    else
    {
        projectPath_ = std::filesystem::current_path(ec);
    }
}

void ModuleResolver::buildModuleRegistry()
{
    if (registryBuilt_)
        return;
    registryBuilt_ = true;

    std::error_code ec;

    // Scan standard library directories
    // NOTE: `compilerPath_` is typically the directory of the compiler executable.
    // In dev builds, the executable often lives in `build/` (macOS/Linux) or
    // `build/Release` (Windows/MSVC). Try a few common layouts.
    std::vector<std::filesystem::path> libDirs = {
        compilerPath_ / "lib",                            // Installed: <compiler_dir>/lib
        compilerPath_.parent_path() / "lib",              // Dev: build -> repo/lib
        compilerPath_.parent_path().parent_path() / "lib" // Dev: build/Release -> repo/lib
    };

    for (const auto &libDir : libDirs)
    {
        if (std::filesystem::exists(libDir, ec))
        {
            scanDirectory(libDir);
        }
    }

    // Scan project modules directory
    std::filesystem::path modulesDir = projectPath_ / "modules";
    if (std::filesystem::exists(modulesDir, ec))
    {
        scanDirectory(modulesDir);
    }

    // Scan custom search paths
    for (const auto &searchPath : searchPaths_)
    {
        if (std::filesystem::exists(searchPath, ec))
        {
            scanDirectory(searchPath);
        }
    }
}

void ModuleResolver::scanDirectory(const std::filesystem::path &dir) const
{
    std::error_code ec;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(dir, ec))
    {
        if (ec)
            continue;

        if (entry.is_regular_file() && entry.path().extension() == ".k")
        {
            auto moduleName = extractModuleName(entry.path());
            if (moduleName)
            {
                // Only register if not already registered (first found wins)
                if (moduleRegistry_.find(*moduleName) == moduleRegistry_.end())
                {
                    moduleRegistry_[*moduleName] = std::filesystem::canonical(entry.path(), ec);
                }
            }
        }
    }
}

std::optional<std::string> ModuleResolver::extractModuleName(const std::filesystem::path &filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    std::string line;
    // Read lines until we find a non-comment, non-empty line
    while (std::getline(file, line))
    {
        // Skip UTF-8 BOM if present
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line = line.substr(3);
        }

        // Trim leading whitespace
        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
        {
            start++;
        }
        line = line.substr(start);

        // Skip empty lines
        if (line.empty())
            continue;

        // Skip single-line comments
        if (line.size() >= 2 && line[0] == '/' && line[1] == '/')
            continue;

        // Check for module declaration
        if (line.size() >= 7 && line.substr(0, 6) == "module" &&
            std::isspace(static_cast<unsigned char>(line[6])))
        {
            // Extract module name
            size_t nameStart = 7;
            while (nameStart < line.size() && std::isspace(static_cast<unsigned char>(line[nameStart])))
            {
                nameStart++;
            }

            size_t nameEnd = nameStart;
            while (nameEnd < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[nameEnd])) || line[nameEnd] == '_'))
            {
                nameEnd++;
            }

            if (nameEnd > nameStart)
            {
                return line.substr(nameStart, nameEnd - nameStart);
            }
        }

        // If first non-comment line is not a module declaration, stop searching
        break;
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ModuleResolver::resolveFromRegistry(const std::string &moduleName) const
{
    // Build registry on first use
    if (!registryBuilt_)
    {
        const_cast<ModuleResolver *>(this)->buildModuleRegistry();
    }

    auto it = moduleRegistry_.find(moduleName);
    if (it != moduleRegistry_.end())
    {
        return it->second;
    }
    return std::nullopt;
}

bool ModuleResolver::isStdModule(const std::string &moduleName) const
{
    // Extract base module name (before any '/')
    std::string baseName = moduleName;
    auto slashPos = moduleName.find('/');
    if (slashPos != std::string::npos)
    {
        baseName = moduleName.substr(0, slashPos);
    }

    return std::find(stdModules_.begin(), stdModules_.end(), baseName) != stdModules_.end();
}

std::vector<std::string> ModuleResolver::getStdModules() const
{
    return stdModules_;
}

void ModuleResolver::addSearchPath(const std::filesystem::path &path)
{
    std::error_code ec;
    auto absPath = std::filesystem::absolute(path, ec);
    if (!ec && std::filesystem::exists(absPath, ec))
    {
        searchPaths_.push_back(absPath);
    }
}

std::optional<std::filesystem::path> ModuleResolver::resolve(
    const std::string &moduleName,
    const std::filesystem::path &currentFile) const
{

    // Handle quoted paths (relative imports) - these start with ./ or ../
    if (moduleName.size() >= 2 && moduleName[0] == '.' &&
        (moduleName[1] == '/' || moduleName[1] == '\\' ||
         (moduleName[1] == '.' && moduleName.size() >= 3)))
    {
        // Relative path - resolve from current file's directory
        std::filesystem::path basePath;
        if (!currentFile.empty())
        {
            basePath = currentFile.parent_path();
        }
        else
        {
            std::error_code ec;
            basePath = std::filesystem::current_path(ec);
        }

        std::filesystem::path resolved = basePath / moduleName;
        // Add .k extension if not present
        if (resolved.extension() != ".k")
        {
            resolved += ".k";
        }

        std::error_code ec;
        if (std::filesystem::exists(resolved, ec))
        {
            return std::filesystem::canonical(resolved, ec);
        }
        return std::nullopt;
    }

    // Try module registry first (scans for 'module' declarations)
    if (auto path = resolveFromRegistry(moduleName))
    {
        return path;
    }

    // Try standard library by convention
    if (auto path = resolveStdLib(moduleName))
    {
        return path;
    }

    // Try project modules
    if (auto path = resolveProjectModule(moduleName))
    {
        return path;
    }

    // Try custom search paths
    if (auto path = resolveFromSearchPaths(moduleName))
    {
        return path;
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ModuleResolver::resolveStdLib(const std::string &moduleName) const
{
    if (compilerPath_.empty())
    {
        return std::nullopt;
    }

    // Handle submodule paths (e.g., "json/parser")
    std::string baseModule = moduleName;
    std::string subPath;
    auto slashPos = moduleName.find('/');
    if (slashPos != std::string::npos)
    {
        baseModule = moduleName.substr(0, slashPos);
        subPath = moduleName.substr(slashPos + 1);
    }

    // Check if it's a known standard library module
    if (!isStdModule(baseModule))
    {
        return std::nullopt;
    }

    // Try multiple possible lib locations
    std::vector<std::filesystem::path> possibleLibDirs = {
        compilerPath_ / "lib" / baseModule,                            // Installed: <compiler_dir>/lib/<module>
        compilerPath_.parent_path() / "lib" / baseModule,              // Dev: build -> repo/lib
        compilerPath_.parent_path().parent_path() / "lib" / baseModule // Dev: build/Release -> repo/lib
    };

    std::error_code ec;
    for (const auto &libDir : possibleLibDirs)
    {
        if (subPath.empty())
        {
            // Main module file: lib/<module>/<module>.k
            std::filesystem::path modulePath = libDir / (baseModule + ".k");
            if (std::filesystem::exists(modulePath, ec))
            {
                return std::filesystem::canonical(modulePath, ec);
            }
        }
        else
        {
            // Submodule: lib/<module>/<subpath>.k
            std::filesystem::path submodulePath = libDir / (subPath + ".k");
            if (std::filesystem::exists(submodulePath, ec))
            {
                return std::filesystem::canonical(submodulePath, ec);
            }
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ModuleResolver::resolveProjectModule(const std::string &moduleName) const
{
    if (projectPath_.empty())
    {
        return std::nullopt;
    }

    std::filesystem::path modulesDir = projectPath_ / "modules";
    std::error_code ec;

    if (!std::filesystem::exists(modulesDir, ec))
    {
        return std::nullopt;
    }

    // Handle submodule paths
    std::string baseModule = moduleName;
    std::string subPath;
    auto slashPos = moduleName.find('/');
    if (slashPos != std::string::npos)
    {
        baseModule = moduleName.substr(0, slashPos);
        subPath = moduleName.substr(slashPos + 1);
    }

    std::filesystem::path moduleDir = modulesDir / baseModule;

    if (subPath.empty())
    {
        // Try mod.k first (convention)
        std::filesystem::path modPath = moduleDir / "mod.k";
        if (std::filesystem::exists(modPath, ec))
        {
            return std::filesystem::canonical(modPath, ec);
        }

        // Then try <module>.k
        std::filesystem::path altPath = moduleDir / (baseModule + ".k");
        if (std::filesystem::exists(altPath, ec))
        {
            return std::filesystem::canonical(altPath, ec);
        }

        // Try src/mod.k or src/<module>.k
        modPath = moduleDir / "src" / "mod.k";
        if (std::filesystem::exists(modPath, ec))
        {
            return std::filesystem::canonical(modPath, ec);
        }

        altPath = moduleDir / "src" / (baseModule + ".k");
        if (std::filesystem::exists(altPath, ec))
        {
            return std::filesystem::canonical(altPath, ec);
        }
    }
    else
    {
        // Submodule: modules/<base>/<subpath>.k
        std::filesystem::path submodulePath = moduleDir / (subPath + ".k");
        if (std::filesystem::exists(submodulePath, ec))
        {
            return std::filesystem::canonical(submodulePath, ec);
        }

        // Also try src/<subpath>.k
        submodulePath = moduleDir / "src" / (subPath + ".k");
        if (std::filesystem::exists(submodulePath, ec))
        {
            return std::filesystem::canonical(submodulePath, ec);
        }
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ModuleResolver::resolveFromSearchPaths(const std::string &moduleName) const
{
    for (const auto &searchPath : searchPaths_)
    {
        std::filesystem::path modulePath = searchPath / (moduleName + ".k");
        std::error_code ec;
        if (std::filesystem::exists(modulePath, ec))
        {
            return std::filesystem::canonical(modulePath, ec);
        }

        // Also try <module>/mod.k
        modulePath = searchPath / moduleName / "mod.k";
        if (std::filesystem::exists(modulePath, ec))
        {
            return std::filesystem::canonical(modulePath, ec);
        }

        // And <module>/<module>.k
        modulePath = searchPath / moduleName / (moduleName + ".k");
        if (std::filesystem::exists(modulePath, ec))
        {
            return std::filesystem::canonical(modulePath, ec);
        }
    }

    return std::nullopt;
}
