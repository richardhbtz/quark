#include <curl/curl.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <atomic>
#include <thread>
#include <unordered_map>

// --- Config ---
static constexpr long DEFAULT_TIMEOUT_SECONDS = 30;                  // default 30s timeout
static std::atomic<long> g_default_timeout{DEFAULT_TIMEOUT_SECONDS};
static std::atomic<size_t> g_max_response_size{2 * 1024 * 1024};     // default 2MB

// --- Global CURL initialization (thread-safe) ---
static std::once_flag curl_init_once;
static std::atomic<bool> curl_initialized{false};

static void ensure_curl_global_init() {
    std::call_once(curl_init_once, []() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
            curl_initialized.store(true, std::memory_order_release);
        }
    });
}

// --- CURL write callback ---
struct ResponseBuffer {
    std::vector<char> data;
    bool size_exceeded = false;
    size_t limit;
    
    ResponseBuffer(size_t lim) : limit(lim) {
        data.reserve(8192);
    }
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, ResponseBuffer* buffer) {
    size_t total_size = size * nmemb;
    size_t maxResp = buffer->limit;
    // Check if adding this data would exceed our limit
    if (buffer->data.size() + total_size > maxResp) {
        buffer->size_exceeded = true;
        // Only add what we can fit
        size_t remaining = maxResp - buffer->data.size();
        if (remaining > 0) {
            buffer->data.insert(buffer->data.end(), 
                               static_cast<const char*>(contents), 
                               static_cast<const char*>(contents) + remaining);
        }
        // Return the actual number of bytes we processed to stop the transfer
        return 0;
    }
    
    // Add all the data
    buffer->data.insert(buffer->data.end(), 
                       static_cast<const char*>(contents), 
                       static_cast<const char*>(contents) + total_size);
    
    return total_size;
}

// --- Core request function ---
// ---------------- Per-thread status & error tracking ----------------
struct ThreadHttpState {
    int last_status = 0;
    std::string last_error; // human readable
};

static thread_local ThreadHttpState tls_state; // thread-local state

static void set_last_error(const std::string& err) {
    tls_state.last_error = err;
}

static void clear_last_error() {
    tls_state.last_error.clear();
}

static void set_last_status(long code) {
    tls_state.last_status = static_cast<int>(code);
}

// Core generalized request implementation
static std::string http_request_curl(const std::string& method, const char* url,
                                    const char* body = nullptr, const char* content_type = nullptr,
                                    const char* const* headers_arr = nullptr, int header_count = 0,
                                    long timeout_override = 0) {
    if (!url) return std::string();
    ensure_curl_global_init();
    if (!curl_initialized.load(std::memory_order_acquire)) return std::string();
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        set_last_error("CURL_INIT_FAILED");
        return std::string();
    }
    
    ResponseBuffer response_buffer(g_max_response_size.load(std::memory_order_relaxed));
    CURLcode res;
    char errbuf[CURL_ERROR_SIZE] = {0};
    
    // Set basic options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    
    long effective_timeout = timeout_override > 0 ? timeout_override : g_default_timeout.load(std::memory_order_relaxed);
    // Set timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, effective_timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, effective_timeout);
    
    // Follow redirects (up to 5)
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    
    // SSL options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // User agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Quark-HTTP-Client/1.0");
    
    // Accept encoding (let CURL handle compression automatically)
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    
    // Method-specific setup
    struct curl_slist* headers = nullptr;
    
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(body));
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
        
        if (content_type) {
            std::string ct_header = "Content-Type: ";
            ct_header += content_type;
            headers = curl_slist_append(headers, ct_header.c_str());
        }
    } else if (method == "GET") {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (method == "PUT" || method == "PATCH" || method == "DELETE" || method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if ((method == "PUT" || method == "PATCH") && body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
            if (content_type) {
                std::string ct_header = "Content-Type: ";
                ct_header += content_type;
                headers = curl_slist_append(headers, ct_header.c_str());
            }
        }
    } else {
        // Arbitrary method; use CUSTOMREQUEST
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
            if (content_type) {
                std::string ct_header = "Content-Type: ";
                ct_header += content_type;
                headers = curl_slist_append(headers, ct_header.c_str());
            }
        }
    }

    // Additional user headers
    if (headers_arr && header_count > 0) {
        for (int i = 0; i < header_count; ++i) {
            if (headers_arr[i]) {
                headers = curl_slist_append(headers, headers_arr[i]);
            }
        }
    }
    
    // Set headers if any
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    
    // Perform the request
    res = curl_easy_perform(curl);

    long status_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    }
    
    // Cleanup
    if (headers) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);
    
    // Check for errors
    if (res != CURLE_OK) {
        set_last_status(status_code); // may be 0 if not reached
        set_last_error(errbuf[0] ? errbuf : curl_easy_strerror(res));
        return std::string();
    }

    if (response_buffer.size_exceeded) {
        set_last_status(status_code);
        set_last_error("HTTP_RESPONSE_TOO_LARGE");
        return std::string();
    }
    set_last_status(status_code);
    clear_last_error();
    
    // Return response as string
    if (response_buffer.data.empty()) {
        return std::string();
    }
    
    return std::string(response_buffer.data.begin(), response_buffer.data.end());
}

// --- C++ API ---
std::string http_get_cpp(const std::string& url) {
    return http_request_curl("GET", url.c_str());
}

std::string http_post_cpp(const std::string& url, const std::string& body,
                          const std::string& content_type = "application/octet-stream") {
    return http_request_curl("POST", url.c_str(),
                             body.empty() ? nullptr : body.c_str(),
                             content_type.c_str());
}

// --- C API ---
extern "C" {

// Duplicate empty string allocated by malloc
static char* dup_empty_malloc() {
    char* s = static_cast<char*>(std::malloc(1));
    if (s) s[0] = '\0';
    return s;
}

char* http_get(const char* url) {
    if (!url) return dup_empty_malloc();
    
    try {
    std::string resp = http_request_curl("GET", url);
        if (resp.empty()) return dup_empty_malloc();
        
        char* out = static_cast<char*>(std::malloc(resp.size() + 1));
        if (!out) return dup_empty_malloc();
        
        std::memcpy(out, resp.data(), resp.size());
        out[resp.size()] = '\0';
        return out;
    } catch (...) {
        return dup_empty_malloc();
    }
}

char* http_post(const char* url, const char* body, const char* content_type) {
    if (!url) return dup_empty_malloc();
    
    try {
        const char* ct = content_type ? content_type : "application/octet-stream";
    std::string resp = http_request_curl("POST", url, body, ct);
        if (resp.empty()) return dup_empty_malloc();
        
        char* out = static_cast<char*>(std::malloc(resp.size() + 1));
        if (!out) return dup_empty_malloc();
        
        std::memcpy(out, resp.data(), resp.size());
        out[resp.size()] = '\0';
        return out;
    } catch (...) {
        return dup_empty_malloc();
    }
}

void http_free(char* response) {
    if (response) std::free(response);
}

void http_cleanup() {
    if (curl_initialized.load(std::memory_order_acquire)) {
        curl_global_cleanup();
        curl_initialized.store(false, std::memory_order_release);
    }
}

// Extended API implementations
char* http_request_ex(const char* method,
                      const char* url,
                      const char* body,
                      const char* content_type,
                      const char* const* headers,
                      int header_count,
                      long timeout_seconds) {
    if (!method || !url) return dup_empty_malloc();
    try {
        std::string resp = http_request_curl(method, url, body, content_type, headers, header_count, timeout_seconds);
        if (resp.empty()) return dup_empty_malloc();
        char* out = static_cast<char*>(std::malloc(resp.size() + 1));
        if (!out) return dup_empty_malloc();
        std::memcpy(out, resp.data(), resp.size());
        out[resp.size()] = '\0';
        return out;
    } catch (...) {
        set_last_error("EXCEPTION");
        return dup_empty_malloc();
    }
}

int http_last_status() {
    return tls_state.last_status;
}

const char* http_last_error() {
    return tls_state.last_error.c_str();
}

void http_clear_last_error() {
    clear_last_error();
}

void http_set_max_response_size(size_t bytes) {
    if (bytes == 0) return; // ignore invalid
    g_max_response_size.store(bytes, std::memory_order_relaxed);
}

} // extern "C"

