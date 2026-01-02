#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <memory>

// Module resolution for the Quark import system
// 
// Import syntax:
//   import json           - Standard library module (by module name)
//   import http           - Standard library module  
//   import discord        - External module from modules/ directory
//   import mymod/sub      - Submodule access
//   import "./local/file" - Relative path import (quoted)
//
// Module declaration (at top of .k file):
//   module json
//
// Resolution order:
//   1. Module registry (scanned from lib/ and modules/)
//   2. Standard library: <compiler_dir>/lib/<module>/<module>.k
//   3. Project modules: <project_dir>/modules/<module>/mod.k
//   4. Relative to current file (quoted paths only)

class ModuleResolver {
public:
    // Initialize with paths to search for modules
    ModuleResolver(const std::filesystem::path& compilerPath,
                   const std::filesystem::path& projectPath);
    
    // Resolve a module name to a file path
    // moduleName: e.g., "json", "http", "discord", "mymod/sub"
    // currentFile: the file doing the import (for relative resolution)
    // Returns the resolved path or nullopt if not found
    std::optional<std::filesystem::path> resolve(const std::string& moduleName,
                                                  const std::filesystem::path& currentFile = "") const;
    
    // Check if a module name is a standard library module
    bool isStdModule(const std::string& moduleName) const;
    
    // Get the list of available standard library modules
    std::vector<std::string> getStdModules() const;
    
    // Add a custom module search path
    void addSearchPath(const std::filesystem::path& path);
    
    // Get the modules directory for the project
    std::filesystem::path getModulesDir() const { return projectPath_ / "modules"; }
    
    // Scan directories and build module registry
    void buildModuleRegistry();
    
    // Extract module name from a .k file (reads first line looking for "module <name>")
    static std::optional<std::string> extractModuleName(const std::filesystem::path& filePath);

private:
    std::filesystem::path compilerPath_;  // Path to compiler executable
    std::filesystem::path projectPath_;   // Path to project root
    std::vector<std::filesystem::path> searchPaths_;
    
    // Module registry: module name -> file path
    mutable std::unordered_map<std::string, std::filesystem::path> moduleRegistry_;
    mutable bool registryBuilt_ = false;
    
    // Standard library modules
    static const std::vector<std::string> stdModules_;
    
    // Try to resolve from module registry
    std::optional<std::filesystem::path> resolveFromRegistry(const std::string& moduleName) const;
    
    // Try to resolve from standard library
    std::optional<std::filesystem::path> resolveStdLib(const std::string& moduleName) const;
    
    // Try to resolve from project modules directory
    std::optional<std::filesystem::path> resolveProjectModule(const std::string& moduleName) const;
    
    // Try to resolve from custom search paths
    std::optional<std::filesystem::path> resolveFromSearchPaths(const std::string& moduleName) const;
    
    // Scan a directory for .k files and register their modules
    void scanDirectory(const std::filesystem::path& dir) const;
};

// Global module resolver instance (set during compilation)
extern std::unique_ptr<ModuleResolver> g_moduleResolver;
