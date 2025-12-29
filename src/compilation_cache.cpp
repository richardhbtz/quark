#include "../include/compilation_cache.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

std::unique_ptr<CompilationCache> g_compilationCache;

namespace {

uint64_t fnv1aHash(const std::string& data) {
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    
    uint64_t hash = FNV_OFFSET;
    for (char c : data) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        hash *= FNV_PRIME;
    }
    return hash;
}

std::string toHexString(uint64_t value) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << value;
    return oss.str();
}

std::string escapeJsonString(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c;
        }
    }
    return oss.str();
}

std::string unescapeJsonString(const std::string& s) {
    std::ostringstream oss;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            switch (s[i + 1]) {
                case '"': oss << '"'; ++i; break;
                case '\\': oss << '\\'; ++i; break;
                case 'n': oss << '\n'; ++i; break;
                case 'r': oss << '\r'; ++i; break;
                case 't': oss << '\t'; ++i; break;
                default: oss << s[i];
            }
        } else {
            oss << s[i];
        }
    }
    return oss.str();
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

} // namespace

CompilationCache::CompilationCache(const std::filesystem::path& cacheDir)
    : cacheDir_(cacheDir)
{
    manifestPath_ = cacheDir_ / MANIFEST_FILENAME;
    manifest_.compilerVersion = COMPILER_VERSION;
    loadManifest();
}

CompilationCache::~CompilationCache() {
    if (manifestDirty_ && enabled_) {
        saveManifest();
    }
}

void CompilationCache::setCacheDirectory(const std::filesystem::path& dir) {
    if (manifestDirty_) {
        saveManifest();
    }
    cacheDir_ = dir;
    manifestPath_ = cacheDir_ / MANIFEST_FILENAME;
    loadManifest();
}

std::string CompilationCache::computeSourceHash(const std::string& source) const {
    return toHexString(fnv1aHash(source));
}

std::string CompilationCache::computeFileHash(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return computeSourceHash(buffer.str());
}

bool CompilationCache::hasValidCache(const std::string& sourcePath,
                                      const std::string& sourceContent,
                                      int optimizationLevel,
                                      bool freestanding) const {
    if (!enabled_) return false;
    
    std::string key = generateCacheKey(sourcePath);
    auto it = manifest_.entries.find(key);
    if (it == manifest_.entries.end()) {
        ++missCount_;
        return false;
    }
    
    std::string currentHash = computeSourceHash(sourceContent);
    if (!validateEntry(it->second, currentHash, optimizationLevel, freestanding)) {
        ++missCount_;
        return false;
    }
    
    if (!std::filesystem::exists(it->second.llvmBitcodePath)) {
        ++missCount_;
        return false;
    }
    
    ++hitCount_;
    return true;
}

std::optional<std::vector<uint8_t>> CompilationCache::getCachedBitcode(const std::string& sourcePath) const {
    if (!enabled_) return std::nullopt;
    
    std::string key = generateCacheKey(sourcePath);
    auto it = manifest_.entries.find(key);
    if (it == manifest_.entries.end()) return std::nullopt;
    
    std::filesystem::path bitcodePath = it->second.llvmBitcodePath;
    if (!std::filesystem::exists(bitcodePath)) return std::nullopt;
    
    std::ifstream file(bitcodePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return std::nullopt;
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> bitcode(size);
    if (!file.read(reinterpret_cast<char*>(bitcode.data()), size)) {
        return std::nullopt;
    }
    
    return bitcode;
}

void CompilationCache::storeBitcode(const std::string& sourcePath,
                                     const std::string& sourceContent,
                                     const std::vector<uint8_t>& bitcode,
                                     int optimizationLevel,
                                     bool freestanding,
                                     const std::vector<std::string>& dependencies) {
    if (!enabled_) return;
    
    ensureCacheDirectory();
    
    std::string key = generateCacheKey(sourcePath);
    std::filesystem::path bitcodePath = getBitcodePath(key);
    
    std::ofstream file(bitcodePath, std::ios::binary);
    if (!file.is_open()) return;
    
    file.write(reinterpret_cast<const char*>(bitcode.data()), bitcode.size());
    file.close();
    
    CacheEntry entry;
    entry.sourceHash = computeSourceHash(sourceContent);
    entry.llvmBitcodePath = bitcodePath.string();
    entry.cacheTime = std::filesystem::file_time_type::clock::now();
    entry.dependencies = dependencies;
    entry.optimizationLevel = optimizationLevel;
    entry.freestanding = freestanding;
    
    try {
        if (std::filesystem::exists(sourcePath)) {
            entry.sourceModTime = std::filesystem::last_write_time(sourcePath);
        }
    } catch (...) {}
    
    manifest_.entries[key] = std::move(entry);
    manifestDirty_ = true;
}

void CompilationCache::invalidate(const std::string& sourcePath) {
    std::string key = generateCacheKey(sourcePath);
    auto it = manifest_.entries.find(key);
    if (it != manifest_.entries.end()) {
        try {
            std::filesystem::remove(it->second.llvmBitcodePath);
        } catch (...) {}
        manifest_.entries.erase(it);
        manifestDirty_ = true;
    }
}

void CompilationCache::invalidateAll() {
    for (const auto& [key, entry] : manifest_.entries) {
        try {
            std::filesystem::remove(entry.llvmBitcodePath);
        } catch (...) {}
    }
    manifest_.entries.clear();
    manifestDirty_ = true;
}

void CompilationCache::loadManifest() {
    manifest_.entries.clear();
    manifest_.version = CACHE_VERSION;
    manifest_.compilerVersion = COMPILER_VERSION;
    
    if (!std::filesystem::exists(manifestPath_)) return;
    
    std::ifstream file(manifestPath_);
    if (!file.is_open()) return;
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    auto findValue = [&content](const std::string& key, size_t start = 0) -> std::pair<std::string, size_t> {
        std::string searchKey = "\"" + key + "\"";
        size_t keyPos = content.find(searchKey, start);
        if (keyPos == std::string::npos) return {"", std::string::npos};
        
        size_t colonPos = content.find(':', keyPos);
        if (colonPos == std::string::npos) return {"", std::string::npos};
        
        size_t valueStart = content.find_first_not_of(" \t\n\r", colonPos + 1);
        if (valueStart == std::string::npos) return {"", std::string::npos};
        
        if (content[valueStart] == '"') {
            size_t valueEnd = valueStart + 1;
            while (valueEnd < content.size()) {
                if (content[valueEnd] == '"' && content[valueEnd - 1] != '\\') break;
                ++valueEnd;
            }
            return {unescapeJsonString(content.substr(valueStart + 1, valueEnd - valueStart - 1)), valueEnd + 1};
        } else if (content[valueStart] == '{' || content[valueStart] == '[') {
            int depth = 1;
            char openChar = content[valueStart];
            char closeChar = (openChar == '{') ? '}' : ']';
            size_t valueEnd = valueStart + 1;
            while (valueEnd < content.size() && depth > 0) {
                if (content[valueEnd] == openChar) ++depth;
                else if (content[valueEnd] == closeChar) --depth;
                ++valueEnd;
            }
            return {content.substr(valueStart, valueEnd - valueStart), valueEnd};
        } else {
            size_t valueEnd = content.find_first_of(",}\n", valueStart);
            if (valueEnd == std::string::npos) valueEnd = content.size();
            return {trim(content.substr(valueStart, valueEnd - valueStart)), valueEnd};
        }
    };
    
    auto [version, _1] = findValue("version");
    if (!version.empty()) {
        try {
            manifest_.version = static_cast<uint32_t>(std::stoul(version));
        } catch (...) {}
    }
    
    auto [compilerVer, _2] = findValue("compilerVersion");
    manifest_.compilerVersion = compilerVer.empty() ? COMPILER_VERSION : compilerVer;
    
    if (manifest_.version != CACHE_VERSION || manifest_.compilerVersion != COMPILER_VERSION) {
        invalidateAll();
        return;
    }
    
    auto [entriesJson, entriesEnd] = findValue("entries");
    if (entriesJson.empty() || entriesJson[0] != '{') return;
    
    size_t pos = 1;
    while (pos < entriesJson.size()) {
        size_t keyStart = entriesJson.find('"', pos);
        if (keyStart == std::string::npos) break;
        
        size_t keyEnd = entriesJson.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        
        std::string entryKey = entriesJson.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        size_t objStart = entriesJson.find('{', keyEnd);
        if (objStart == std::string::npos) break;
        
        int depth = 1;
        size_t objEnd = objStart + 1;
        while (objEnd < entriesJson.size() && depth > 0) {
            if (entriesJson[objEnd] == '{') ++depth;
            else if (entriesJson[objEnd] == '}') --depth;
            ++objEnd;
        }
        
        std::string entryJson = entriesJson.substr(objStart, objEnd - objStart);
        
        CacheEntry entry;
        auto extractField = [&entryJson](const std::string& field) -> std::string {
            std::string search = "\"" + field + "\"";
            size_t pos = entryJson.find(search);
            if (pos == std::string::npos) return "";
            size_t colon = entryJson.find(':', pos);
            if (colon == std::string::npos) return "";
            size_t valStart = entryJson.find_first_not_of(" \t\n\r", colon + 1);
            if (valStart == std::string::npos) return "";
            if (entryJson[valStart] == '"') {
                size_t valEnd = valStart + 1;
                while (valEnd < entryJson.size() && !(entryJson[valEnd] == '"' && entryJson[valEnd-1] != '\\')) ++valEnd;
                return unescapeJsonString(entryJson.substr(valStart + 1, valEnd - valStart - 1));
            }
            size_t valEnd = entryJson.find_first_of(",}", valStart);
            return trim(entryJson.substr(valStart, valEnd - valStart));
        };
        
        entry.sourceHash = extractField("sourceHash");
        entry.llvmBitcodePath = extractField("llvmBitcodePath");
        
        std::string optLevel = extractField("optimizationLevel");
        if (!optLevel.empty()) {
            try { entry.optimizationLevel = std::stoi(optLevel); } catch (...) {}
        }
        
        std::string freestand = extractField("freestanding");
        entry.freestanding = (freestand == "true" || freestand == "1");
        
        if (entry.isValid()) {
            manifest_.entries[entryKey] = std::move(entry);
        }
        
        pos = objEnd;
    }
}

void CompilationCache::saveManifest() {
    ensureCacheDirectory();
    
    std::ofstream file(manifestPath_);
    if (!file.is_open()) return;
    
    file << "{\n";
    file << "  \"version\": " << manifest_.version << ",\n";
    file << "  \"compilerVersion\": \"" << escapeJsonString(manifest_.compilerVersion) << "\",\n";
    file << "  \"entries\": {\n";
    
    bool first = true;
    for (const auto& [key, entry] : manifest_.entries) {
        if (!first) file << ",\n";
        first = false;
        
        file << "    \"" << escapeJsonString(key) << "\": {\n";
        file << "      \"sourceHash\": \"" << escapeJsonString(entry.sourceHash) << "\",\n";
        file << "      \"llvmBitcodePath\": \"" << escapeJsonString(entry.llvmBitcodePath) << "\",\n";
        file << "      \"optimizationLevel\": " << entry.optimizationLevel << ",\n";
        file << "      \"freestanding\": " << (entry.freestanding ? "true" : "false");
        
        if (!entry.dependencies.empty()) {
            file << ",\n      \"dependencies\": [";
            for (size_t i = 0; i < entry.dependencies.size(); ++i) {
                if (i > 0) file << ", ";
                file << "\"" << escapeJsonString(entry.dependencies[i]) << "\"";
            }
            file << "]";
        }
        
        file << "\n    }";
    }
    
    file << "\n  }\n";
    file << "}\n";
    
    manifestDirty_ = false;
}

CompilationCache::CacheStats CompilationCache::getStats() const {
    CacheStats stats;
    stats.hitCount = hitCount_;
    stats.missCount = missCount_;
    stats.totalEntries = manifest_.entries.size();
    
    for (const auto& [key, entry] : manifest_.entries) {
        if (std::filesystem::exists(entry.llvmBitcodePath)) {
            ++stats.validEntries;
            try {
                stats.totalSizeBytes += std::filesystem::file_size(entry.llvmBitcodePath);
            } catch (...) {}
        }
    }
    
    return stats;
}

void CompilationCache::resetStats() {
    hitCount_ = 0;
    missCount_ = 0;
}

void CompilationCache::pruneOldEntries(std::chrono::hours maxAge) {
    auto now = std::filesystem::file_time_type::clock::now();
    std::vector<std::string> toRemove;
    
    for (const auto& [key, entry] : manifest_.entries) {
        auto age = std::chrono::duration_cast<std::chrono::hours>(now - entry.cacheTime);
        if (age > maxAge) {
            toRemove.push_back(key);
        }
    }
    
    for (const auto& key : toRemove) {
        auto it = manifest_.entries.find(key);
        if (it != manifest_.entries.end()) {
            try {
                std::filesystem::remove(it->second.llvmBitcodePath);
            } catch (...) {}
            manifest_.entries.erase(it);
        }
    }
    
    if (!toRemove.empty()) {
        manifestDirty_ = true;
    }
}

void CompilationCache::pruneBySize(size_t maxSizeBytes) {
    std::vector<std::pair<std::string, std::filesystem::file_time_type>> entriesByTime;
    size_t totalSize = 0;
    
    for (const auto& [key, entry] : manifest_.entries) {
        if (std::filesystem::exists(entry.llvmBitcodePath)) {
            try {
                totalSize += std::filesystem::file_size(entry.llvmBitcodePath);
                entriesByTime.emplace_back(key, entry.cacheTime);
            } catch (...) {}
        }
    }
    
    if (totalSize <= maxSizeBytes) return;
    
    std::sort(entriesByTime.begin(), entriesByTime.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    for (const auto& [key, _] : entriesByTime) {
        if (totalSize <= maxSizeBytes) break;
        
        auto it = manifest_.entries.find(key);
        if (it != manifest_.entries.end()) {
            try {
                size_t fileSize = std::filesystem::file_size(it->second.llvmBitcodePath);
                std::filesystem::remove(it->second.llvmBitcodePath);
                totalSize -= fileSize;
            } catch (...) {}
            manifest_.entries.erase(it);
            manifestDirty_ = true;
        }
    }
}

std::string CompilationCache::generateCacheKey(const std::string& sourcePath) const {
    std::filesystem::path p(sourcePath);
    std::string normalized;
    try {
        normalized = std::filesystem::absolute(p).lexically_normal().string();
    } catch (...) {
        normalized = sourcePath;
    }
    return toHexString(fnv1aHash(normalized));
}

std::filesystem::path CompilationCache::getBitcodePath(const std::string& cacheKey) const {
    return cacheDir_ / cacheKey;
}

bool CompilationCache::validateEntry(const CacheEntry& entry,
                                      const std::string& currentHash,
                                      int optimizationLevel,
                                      bool freestanding) const {
    if (entry.sourceHash != currentHash) return false;
    if (entry.optimizationLevel != optimizationLevel) return false;
    if (entry.freestanding != freestanding) return false;
    
    for (const auto& dep : entry.dependencies) {
        if (!std::filesystem::exists(dep)) return false;
    }
    
    return true;
}

void CompilationCache::ensureCacheDirectory() {
    if (!std::filesystem::exists(cacheDir_)) {
        try {
            std::filesystem::create_directories(cacheDir_);
        } catch (...) {}
    }
}
