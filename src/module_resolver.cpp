#include "../include/module_resolver.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>

std::unique_ptr<ModuleResolver> g_moduleResolver;

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

    std::error_code ec;
    if (!compilerPath_.empty())
    {
        compilerPath_ = std::filesystem::absolute(compilerPath_, ec);
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

    std::vector<std::filesystem::path> libDirs = {
        compilerPath_ / "lib",
        compilerPath_.parent_path() / "lib",
        compilerPath_.parent_path().parent_path() / "lib"};

    for (const auto &libDir : libDirs)
    {
        if (std::filesystem::exists(libDir, ec))
        {
            scanDirectory(libDir);
        }
    }

    std::filesystem::path modulesDir = projectPath_ / "modules";
    if (std::filesystem::exists(modulesDir, ec))
    {
        scanDirectory(modulesDir);
    }

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

    while (std::getline(file, line))
    {

        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
        {
            line = line.substr(3);
        }

        size_t start = 0;
        while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start])))
        {
            start++;
        }
        line = line.substr(start);

        if (line.empty())
            continue;

        if (line.size() >= 2 && line[0] == '/' && line[1] == '/')
            continue;

        if (line.size() >= 7 && line.substr(0, 6) == "module" &&
            std::isspace(static_cast<unsigned char>(line[6])))
        {

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

        break;
    }

    return std::nullopt;
}

std::optional<std::filesystem::path> ModuleResolver::resolveFromRegistry(const std::string &moduleName) const
{

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

    if (moduleName.size() >= 2 && moduleName[0] == '.' &&
        (moduleName[1] == '/' || moduleName[1] == '\\' ||
         (moduleName[1] == '.' && moduleName.size() >= 3)))
    {

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

    if (auto path = resolveFromRegistry(moduleName))
    {
        return path;
    }

    if (auto path = resolveStdLib(moduleName))
    {
        return path;
    }

    if (auto path = resolveProjectModule(moduleName))
    {
        return path;
    }

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

    std::string baseModule = moduleName;
    std::string subPath;
    auto slashPos = moduleName.find('/');
    if (slashPos != std::string::npos)
    {
        baseModule = moduleName.substr(0, slashPos);
        subPath = moduleName.substr(slashPos + 1);
    }

    if (!isStdModule(baseModule))
    {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> possibleLibDirs = {
        compilerPath_ / "lib" / baseModule,
        compilerPath_.parent_path() / "lib" / baseModule,
        compilerPath_.parent_path().parent_path() / "lib" / baseModule};

    std::error_code ec;
    for (const auto &libDir : possibleLibDirs)
    {
        if (subPath.empty())
        {

            std::filesystem::path modulePath = libDir / (baseModule + ".k");
            if (std::filesystem::exists(modulePath, ec))
            {
                return std::filesystem::canonical(modulePath, ec);
            }
        }
        else
        {

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

        std::filesystem::path modPath = moduleDir / "mod.k";
        if (std::filesystem::exists(modPath, ec))
        {
            return std::filesystem::canonical(modPath, ec);
        }

        std::filesystem::path altPath = moduleDir / (baseModule + ".k");
        if (std::filesystem::exists(altPath, ec))
        {
            return std::filesystem::canonical(altPath, ec);
        }

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

        std::filesystem::path submodulePath = moduleDir / (subPath + ".k");
        if (std::filesystem::exists(submodulePath, ec))
        {
            return std::filesystem::canonical(submodulePath, ec);
        }

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

        modulePath = searchPath / moduleName / "mod.k";
        if (std::filesystem::exists(modulePath, ec))
        {
            return std::filesystem::canonical(modulePath, ec);
        }

        modulePath = searchPath / moduleName / (moduleName + ".k");
        if (std::filesystem::exists(modulePath, ec))
        {
            return std::filesystem::canonical(modulePath, ec);
        }
    }

    return std::nullopt;
}
