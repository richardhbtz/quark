#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <memory>
#include <optional>
#include <chrono>
#include <cstdint>

struct CacheEntry {
    std::string sourceHash;
    std::string llvmBitcodePath;
    std::filesystem::file_time_type sourceModTime;
    std::filesystem::file_time_type cacheTime;
    std::vector<std::string> dependencies;
    int optimizationLevel;
    bool freestanding;
    
    bool isValid() const { return !sourceHash.empty() && !llvmBitcodePath.empty(); }
};

struct CacheManifest {
    uint32_t version = 1;
    std::string compilerVersion;
    std::unordered_map<std::string, CacheEntry> entries;
};

class CompilationCache {
public:
    explicit CompilationCache(const std::filesystem::path& cacheDir = ".quark_cache");
    ~CompilationCache();
    
    void setCacheEnabled(bool enabled) { enabled_ = enabled; }
    bool isCacheEnabled() const { return enabled_; }
    
    void setCacheDirectory(const std::filesystem::path& dir);
    std::filesystem::path getCacheDirectory() const { return cacheDir_; }
    
    std::string computeSourceHash(const std::string& source) const;
    std::string computeFileHash(const std::filesystem::path& path) const;
    
    bool hasValidCache(const std::string& sourcePath, 
                       const std::string& sourceContent,
                       int optimizationLevel,
                       bool freestanding) const;
    
    std::optional<std::vector<uint8_t>> getCachedBitcode(const std::string& sourcePath) const;
    
    void storeBitcode(const std::string& sourcePath,
                      const std::string& sourceContent,
                      const std::vector<uint8_t>& bitcode,
                      int optimizationLevel,
                      bool freestanding,
                      const std::vector<std::string>& dependencies = {});
    
    void invalidate(const std::string& sourcePath);
    void invalidateAll();
    
    void loadManifest();
    void saveManifest();
    
    struct CacheStats {
        size_t totalEntries = 0;
        size_t validEntries = 0;
        size_t totalSizeBytes = 0;
        size_t hitCount = 0;
        size_t missCount = 0;
    };
    
    CacheStats getStats() const;
    void resetStats();
    
    void pruneOldEntries(std::chrono::hours maxAge = std::chrono::hours(24 * 7));
    void pruneBySize(size_t maxSizeBytes);

private:
    std::filesystem::path cacheDir_;
    std::filesystem::path manifestPath_;
    CacheManifest manifest_;
    bool enabled_ = true;
    bool manifestDirty_ = false;
    
    mutable size_t hitCount_ = 0;
    mutable size_t missCount_ = 0;
    
    std::string generateCacheKey(const std::string& sourcePath) const;
    std::filesystem::path getBitcodePath(const std::string& cacheKey) const;
    bool validateEntry(const CacheEntry& entry, 
                       const std::string& currentHash,
                       int optimizationLevel,
                       bool freestanding) const;
    void ensureCacheDirectory();
    
    static constexpr uint32_t CACHE_VERSION = 1;
    static constexpr const char* MANIFEST_FILENAME = "manifest.json";
    static constexpr const char* COMPILER_VERSION = "0.1.0";
};

extern std::unique_ptr<CompilationCache> g_compilationCache;
