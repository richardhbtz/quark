#include "json.h"
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <memory>

extern "C" {
static thread_local struct {
    std::string msg;
    int code = 0;
    int offset = 0;
    int line = 0;
    int row = 0;
} tls_err;

static void set_error_from_result(const json_parse_result_s& r) {
    tls_err.code = static_cast<int>(r.error);
    tls_err.offset = static_cast<int>(r.error_offset);
    tls_err.line = static_cast<int>(r.error_line_no);
    tls_err.row = static_cast<int>(r.error_row_no);
    switch (r.error) {
        default: tls_err.msg = "json_parse_error_unknown"; break;
        case json_parse_error_none: tls_err.msg.clear(); break;
        case json_parse_error_expected_comma_or_closing_bracket: tls_err.msg = "expected_comma_or_closing_bracket"; break;
        case json_parse_error_expected_colon: tls_err.msg = "expected_colon"; break;
        case json_parse_error_expected_opening_quote: tls_err.msg = "expected_opening_quote"; break;
        case json_parse_error_invalid_string_escape_sequence: tls_err.msg = "invalid_string_escape_sequence"; break;
        case json_parse_error_invalid_number_format: tls_err.msg = "invalid_number_format"; break;
        case json_parse_error_invalid_value: tls_err.msg = "invalid_value"; break;
        case json_parse_error_premature_end_of_buffer: tls_err.msg = "premature_end_of_buffer"; break;
        case json_parse_error_invalid_string: tls_err.msg = "invalid_string"; break;
        case json_parse_error_allocator_failed: tls_err.msg = "allocator_failed"; break;
        case json_parse_error_unexpected_trailing_characters: tls_err.msg = "unexpected_trailing_characters"; break;
    }
}

static void clear_error() {
    tls_err = {};
}

static char* dup_empty() {
    char* s = (char*)std::malloc(1);
    if (s) s[0] = '\0';
    return s;
}

static char* dup_str(const std::string& s) {
    char* out = (char*)std::malloc(s.size() + 1);
    if (!out) return dup_empty();
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

char* qjson_minify(const char* text) {
    if (!text) return dup_empty();
    clear_error();
    json_parse_result_s r{};
    json_value_s* v = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!v) {
        set_error_from_result(r);
        return dup_empty();
    }
    size_t out_sz = 0;
    void* out = json_write_minified(v, &out_sz);
    std::free(v);
    if (!out) return dup_empty();
    char* s = (char*)std::malloc(out_sz + 1);
    if (!s) { std::free(out); return dup_empty(); }
    std::memcpy(s, out, out_sz);
    s[out_sz] = '\0';
    std::free(out);
    return s;
}

char* qjson_pretty(const char* text, const char* indent, const char* newline) {
    if (!text) return dup_empty();
    clear_error();
    const char* ind = indent && indent[0] ? indent : "  ";
    const char* nl = newline && newline[0] ? newline : "\n";
    json_parse_result_s r{};
    json_value_s* v = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!v) {
        set_error_from_result(r);
        return dup_empty();
    }
    size_t out_sz = 0;
    void* out = json_write_pretty(v, ind, nl, &out_sz);
    std::free(v);
    if (!out) return dup_empty();
    char* s = (char*)std::malloc(out_sz + 1);
    if (!s) { std::free(out); return dup_empty(); }
    std::memcpy(s, out, out_sz);
    s[out_sz] = '\0';
    std::free(out);
    return s;
}

int qjson_validate(const char* text) {
    if (!text) return 0;
    clear_error();
    json_parse_result_s r{};
    json_value_s* v = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!v) { set_error_from_result(r); return 0; }
    std::free(v);
    return 1;
}

static const json_value_s* find_in_object(const json_object_s* obj, const char* key) {
    for (auto* e = obj->start; e; e = e->next) {
        if (!e->name || !e->name->string) continue;
        if (std::strncmp(e->name->string, key, e->name->string_size) == 0 && key[e->name->string_size] == '\0')
            return e->value;
    }
    return nullptr;
}

static const json_value_s* find_in_array(const json_array_s* arr, size_t idx) {
    size_t i = 0;
    for (auto* e = arr->start; e; e = e->next, ++i) {
        if (i == idx) return e->value;
    }
    return nullptr;
}

static bool parse_path_token(const char* s, size_t& pos, std::string& key, bool& is_index, size_t& index) {
    key.clear(); is_index = false; index = 0;
    if (s[pos] == '[') {
        ++pos; size_t start = pos;
        while (std::isdigit((unsigned char)s[pos])) ++pos;
        if (pos == start || s[pos] != ']') return false;
        is_index = true;
        index = (size_t)std::strtoull(std::string(s + start, pos - start).c_str(), nullptr, 10);
        ++pos;
        return true;
    }
    size_t start = pos;
    while (s[pos] && s[pos] != '.' && s[pos] != '[') ++pos;
    if (pos == start) return false;
    key.assign(s + start, pos - start);
    return true;
}

char* qjson_get(const char* text, const char* path) {
    if (!text || !path) return dup_empty();
    clear_error();
    json_parse_result_s r{};
    json_value_s* root = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!root) { set_error_from_result(r); return dup_empty(); }

    const json_value_s* cur = root;
    size_t pos = 0; std::string key; bool is_index; size_t idx;
    while (path[pos]) {
        if (!parse_path_token(path, pos, key, is_index, idx)) { std::free(root); return dup_empty(); }
        if (is_index) {
            auto* arr = json_value_as_array((json_value_s*)cur);
            if (!arr) { std::free(root); return dup_empty(); }
            cur = find_in_array(arr, idx);
            if (!cur) { std::free(root); return dup_empty(); }
        } else {
            auto* obj = json_value_as_object((json_value_s*)cur);
            if (!obj) { std::free(root); return dup_empty(); }
            cur = find_in_object(obj, key.c_str());
            if (!cur) { std::free(root); return dup_empty(); }
        }
        if (path[pos] == '.') ++pos;
    }

    size_t out_sz = 0;
    void* out = json_write_minified(cur, &out_sz);
    std::free(root);
    if (!out) return dup_empty();
    char* s = (char*)std::malloc(out_sz + 1);
    if (!s) { std::free(out); return dup_empty(); }
    std::memcpy(s, out, out_sz);
    s[out_sz] = '\0';
    std::free(out);
    return s;
}

static char* dup_unescaped_json_string(const json_string_s* js) {
    if (!js || !js->string) return dup_empty();
    // The single-header parser stores the JSON string's content unescaped,
    // and provides string and string_size without quotes.
    char* out = (char*)std::malloc(js->string_size + 1);
    if (!out) return dup_empty();
    std::memcpy(out, js->string, js->string_size);
    out[js->string_size] = '\0';
    return out;
}

char* qjson_get_string(const char* text, const char* path) {
    if (!text || !path) return dup_empty();
    clear_error();
    json_parse_result_s r{};
    json_value_s* root = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!root) { set_error_from_result(r); return dup_empty(); }

    const json_value_s* cur = root;
    size_t pos = 0; std::string key; bool is_index; size_t idx;
    while (path[pos]) {
        if (!parse_path_token(path, pos, key, is_index, idx)) { std::free(root); return dup_empty(); }
        if (is_index) {
            auto* arr = json_value_as_array((json_value_s*)cur);
            if (!arr) { std::free(root); return dup_empty(); }
            cur = find_in_array(arr, idx);
            if (!cur) { std::free(root); return dup_empty(); }
        } else {
            auto* obj = json_value_as_object((json_value_s*)cur);
            if (!obj) { std::free(root); return dup_empty(); }
            cur = find_in_object(obj, key.c_str());
            if (!cur) { std::free(root); return dup_empty(); }
        }
        if (path[pos] == '.') ++pos;
    }

    char* out = nullptr;
    if (auto* s = json_value_as_string((json_value_s*)cur)) {
        out = dup_unescaped_json_string(s);
    } else {
        // Not a string; return empty
        out = dup_empty();
    }
    std::free(root);
    return out;
}

// Get the length of a JSON array at the given path
int qjson_get_array_length(const char* text, const char* path) {
    if (!text) return 0;
    clear_error();
    json_parse_result_s r{};
    json_value_s* root = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!root) { set_error_from_result(r); return 0; }

    const json_value_s* cur = root;
    
    // Navigate to path if provided
    if (path && path[0]) {
        size_t pos = 0; std::string key; bool is_index; size_t idx;
        while (path[pos]) {
            if (!parse_path_token(path, pos, key, is_index, idx)) { std::free(root); return 0; }
            if (is_index) {
                auto* arr = json_value_as_array((json_value_s*)cur);
                if (!arr) { std::free(root); return 0; }
                cur = find_in_array(arr, idx);
                if (!cur) { std::free(root); return 0; }
            } else {
                auto* obj = json_value_as_object((json_value_s*)cur);
                if (!obj) { std::free(root); return 0; }
                cur = find_in_object(obj, key.c_str());
                if (!cur) { std::free(root); return 0; }
            }
            if (path[pos] == '.') ++pos;
        }
    }

    auto* arr = json_value_as_array((json_value_s*)cur);
    if (!arr) { std::free(root); return 0; }
    
    int count = 0;
    for (auto* e = arr->start; e; e = e->next) ++count;
    
    std::free(root);
    return count;
}

// Get an item from a JSON array at the given path by index (returns JSON string)
char* qjson_get_array_item(const char* text, const char* path, int index) {
    if (!text || index < 0) return dup_empty();
    clear_error();
    json_parse_result_s r{};
    json_value_s* root = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!root) { set_error_from_result(r); return dup_empty(); }

    const json_value_s* cur = root;
    
    // Navigate to path if provided
    if (path && path[0]) {
        size_t pos = 0; std::string key; bool is_index; size_t idx;
        while (path[pos]) {
            if (!parse_path_token(path, pos, key, is_index, idx)) { std::free(root); return dup_empty(); }
            if (is_index) {
                auto* arr = json_value_as_array((json_value_s*)cur);
                if (!arr) { std::free(root); return dup_empty(); }
                cur = find_in_array(arr, idx);
                if (!cur) { std::free(root); return dup_empty(); }
            } else {
                auto* obj = json_value_as_object((json_value_s*)cur);
                if (!obj) { std::free(root); return dup_empty(); }
                cur = find_in_object(obj, key.c_str());
                if (!cur) { std::free(root); return dup_empty(); }
            }
            if (path[pos] == '.') ++pos;
        }
    }

    auto* arr = json_value_as_array((json_value_s*)cur);
    if (!arr) { std::free(root); return dup_empty(); }
    
    cur = find_in_array(arr, (size_t)index);
    if (!cur) { std::free(root); return dup_empty(); }

    // If it's a string, return unescaped value
    if (auto* s = json_value_as_string((json_value_s*)cur)) {
        char* out = dup_unescaped_json_string(s);
        std::free(root);
        return out;
    }

    // Otherwise return serialized JSON
    size_t out_sz = 0;
    void* out = json_write_minified(cur, &out_sz);
    std::free(root);
    if (!out) return dup_empty();
    char* s = (char*)std::malloc(out_sz + 1);
    if (!s) { std::free(out); return dup_empty(); }
    std::memcpy(s, out, out_sz);
    s[out_sz] = '\0';
    std::free(out);
    return s;
}

// Get a string item from a JSON array at the given path by index
char* qjson_get_array_string(const char* text, const char* path, int index) {
    if (!text || index < 0) return dup_empty();
    clear_error();
    json_parse_result_s r{};
    json_value_s* root = json_parse_ex(text, std::strlen(text), json_parse_flags_default, nullptr, nullptr, &r);
    if (!root) { set_error_from_result(r); return dup_empty(); }

    const json_value_s* cur = root;
    
    // Navigate to path if provided
    if (path && path[0]) {
        size_t pos = 0; std::string key; bool is_index; size_t idx;
        while (path[pos]) {
            if (!parse_path_token(path, pos, key, is_index, idx)) { std::free(root); return dup_empty(); }
            if (is_index) {
                auto* arr = json_value_as_array((json_value_s*)cur);
                if (!arr) { std::free(root); return dup_empty(); }
                cur = find_in_array(arr, idx);
                if (!cur) { std::free(root); return dup_empty(); }
            } else {
                auto* obj = json_value_as_object((json_value_s*)cur);
                if (!obj) { std::free(root); return dup_empty(); }
                cur = find_in_object(obj, key.c_str());
                if (!cur) { std::free(root); return dup_empty(); }
            }
            if (path[pos] == '.') ++pos;
        }
    }

    auto* arr = json_value_as_array((json_value_s*)cur);
    if (!arr) { std::free(root); return dup_empty(); }
    
    cur = find_in_array(arr, (size_t)index);
    if (!cur) { std::free(root); return dup_empty(); }

    char* out = nullptr;
    if (auto* s = json_value_as_string((json_value_s*)cur)) {
        out = dup_unescaped_json_string(s);
    } else {
        out = dup_empty();
    }
    std::free(root);
    return out;
}

const char* qjson_last_error() { return tls_err.msg.c_str(); }
int qjson_last_error_code() { return tls_err.code; }
int qjson_last_error_offset() { return tls_err.offset; }
int qjson_last_error_line() { return tls_err.line; }
int qjson_last_error_row() { return tls_err.row; }
void qjson_clear_last_error() { clear_error(); }

void qjson_free(char* ptr) { if (ptr) std::free(ptr); }

} // extern "C"
