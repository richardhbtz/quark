#include "quark_toml.h"

#define TOML_EXCEPTIONS 0
#include "toml.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <atomic>
#include <string_view>
#include <limits>

struct qtoml_document {
    toml::table root;
};

namespace {

struct TomlThreadState {
    std::string last_error;
};

thread_local TomlThreadState g_toml_state;

std::mutex g_doc_mutex;
std::unordered_map<int, std::shared_ptr<qtoml_document>> g_documents;
std::atomic<int> g_next_handle{1};

static std::shared_ptr<qtoml_document> get_document(int handle) {
    if (handle <= 0) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(g_doc_mutex);
    auto it = g_documents.find(handle);
    if (it == g_documents.end()) {
        return nullptr;
    }
    return it->second;
}

struct PathToken {
    std::string key;
    bool is_index = false;
    size_t index = 0;
};

static void set_error(const std::string& message) {
    g_toml_state.last_error = message;
}

static void clear_error() {
    g_toml_state.last_error.clear();
}

static char* dup_empty_malloc() {
    char* buffer = static_cast<char*>(std::malloc(1));
    if (buffer) {
        buffer[0] = '\0';
    }
    return buffer;
}

static char* dup_string_malloc(const std::string& value) {
    char* buffer = static_cast<char*>(std::malloc(value.size() + 1));
    if (!buffer) {
        set_error("QTOML_ALLOC_FAILED");
        return dup_empty_malloc();
    }
    std::memcpy(buffer, value.data(), value.size());
    buffer[value.size()] = '\0';
    return buffer;
}

static bool parse_path_tokens(const char* path, std::vector<PathToken>& out_tokens) {
    out_tokens.clear();
    if (!path || !*path) {
        return true;
    }

    size_t pos = 0;
    while (path[pos]) {
        if (path[pos] == '.') {
            return false;
        }

        PathToken token;
        if (path[pos] == '[') {
            ++pos;
            size_t start = pos;
            while (std::isdigit(static_cast<unsigned char>(path[pos]))) {
                ++pos;
            }
            if (start == pos || path[pos] != ']') {
                return false;
            }
            token.is_index = true;
            token.index = static_cast<size_t>(std::strtoull(path + start, nullptr, 10));
            ++pos; // skip ']'
        } else {
            size_t start = pos;
            while (path[pos] && path[pos] != '.' && path[pos] != '[') {
                ++pos;
            }
            if (start == pos) {
                return false;
            }
            token.key.assign(path + start, pos - start);
        }

        out_tokens.emplace_back(std::move(token));
        if (path[pos] == '.') {
            ++pos;
            if (!path[pos]) {
                return false;
            }
        }
    }

    return true;
}

static toml::node_view<toml::node> view_at(toml::table& root, const char* path) {
    if (!path || !*path) {
        return toml::node_view<toml::node>{ root };
    }
    return root.at_path(path);
}

static toml::node_view<const toml::node> view_at_const(const toml::table& root, const char* path) {
    if (!path || !*path) {
        return toml::node_view<const toml::node>{ const_cast<toml::table&>(root) };
    }
    return root.at_path(path);
}

static toml::node* ensure_parent_for_assignment(toml::table& root, const std::vector<PathToken>& tokens) {
    if (tokens.empty()) {
        return &root;
    }

    toml::node* current = &root;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const PathToken& token = tokens[i];
        if (token.is_index) {
            auto* arr = current->as_array();
            if (!arr) {
                set_error("QTOML_TYPE_MISMATCH");
                return nullptr;
            }
            toml::node* next = arr->get(token.index);
            if (!next) {
                set_error("QTOML_INDEX_OUT_OF_RANGE");
                return nullptr;
            }
            current = next;
        } else {
            auto* tbl = current->as_table();
            if (!tbl) {
                set_error("QTOML_TYPE_MISMATCH");
                return nullptr;
            }
            auto child_view = (*tbl)[token.key];
            if (!child_view) {
                tbl->insert(token.key, toml::table{});
                child_view = (*tbl)[token.key];
            }
            auto* child_node = child_view.node();
            if (!child_node) {
                set_error("QTOML_TYPE_MISMATCH");
                return nullptr;
            }
            if (!child_node->is_table()) {
                set_error("QTOML_TYPE_MISMATCH");
                return nullptr;
            }
            current = child_node;
        }
    }
    return current;
}

static bool assign_int64(toml::node& parent, const PathToken& final_token, int64_t value) {
    if (final_token.is_index) {
        auto* arr = parent.as_array();
        if (!arr) {
            set_error("QTOML_TYPE_MISMATCH");
            return false;
        }
        if (final_token.index < arr->size()) {
            arr->replace(arr->cbegin() + static_cast<ptrdiff_t>(final_token.index), value);
            clear_error();
            return true;
        }
        if (final_token.index == arr->size()) {
            arr->push_back(value);
            clear_error();
            return true;
        }
        set_error("QTOML_INDEX_OUT_OF_RANGE");
        return false;
    }

    auto* tbl = parent.as_table();
    if (!tbl) {
        set_error("QTOML_TYPE_MISMATCH");
        return false;
    }
    tbl->insert_or_assign(final_token.key, value);
    clear_error();
    return true;
}

static bool assign_float64(toml::node& parent, const PathToken& final_token, double value) {
    if (final_token.is_index) {
        auto* arr = parent.as_array();
        if (!arr) {
            set_error("QTOML_TYPE_MISMATCH");
            return false;
        }
        if (final_token.index < arr->size()) {
            arr->replace(arr->cbegin() + static_cast<ptrdiff_t>(final_token.index), value);
            clear_error();
            return true;
        }
        if (final_token.index == arr->size()) {
            arr->push_back(value);
            clear_error();
            return true;
        }
        set_error("QTOML_INDEX_OUT_OF_RANGE");
        return false;
    }

    auto* tbl = parent.as_table();
    if (!tbl) {
        set_error("QTOML_TYPE_MISMATCH");
        return false;
    }
    tbl->insert_or_assign(final_token.key, value);
    clear_error();
    return true;
}

static bool assign_bool(toml::node& parent, const PathToken& final_token, bool value) {
    if (final_token.is_index) {
        auto* arr = parent.as_array();
        if (!arr) {
            set_error("QTOML_TYPE_MISMATCH");
            return false;
        }
        if (final_token.index < arr->size()) {
            arr->replace(arr->cbegin() + static_cast<ptrdiff_t>(final_token.index), value);
            clear_error();
            return true;
        }
        if (final_token.index == arr->size()) {
            arr->push_back(value);
            clear_error();
            return true;
        }
        set_error("QTOML_INDEX_OUT_OF_RANGE");
        return false;
    }

    auto* tbl = parent.as_table();
    if (!tbl) {
        set_error("QTOML_TYPE_MISMATCH");
        return false;
    }
    tbl->insert_or_assign(final_token.key, value);
    clear_error();
    return true;
}

static bool assign_string(toml::node& parent, const PathToken& final_token, const char* value) {
    std::string str = value ? std::string(value) : std::string{};
    if (final_token.is_index) {
        auto* arr = parent.as_array();
        if (!arr) {
            set_error("QTOML_TYPE_MISMATCH");
            return false;
        }
        if (final_token.index < arr->size()) {
            arr->replace(arr->cbegin() + static_cast<ptrdiff_t>(final_token.index), str);
            clear_error();
            return true;
        }
        if (final_token.index == arr->size()) {
            arr->push_back(str);
            clear_error();
            return true;
        }
        set_error("QTOML_INDEX_OUT_OF_RANGE");
        return false;
    }

    auto* tbl = parent.as_table();
    if (!tbl) {
        set_error("QTOML_TYPE_MISMATCH");
        return false;
    }
    tbl->insert_or_assign(final_token.key, str);
    clear_error();
    return true;
}

static bool write_file(const char* path, const std::string& contents) {
    if (!path) {
        set_error("QTOML_INVALID_PATH");
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        set_error("QTOML_SAVE_OPEN_FAILED");
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file.good()) {
        set_error("QTOML_SAVE_WRITE_FAILED");
        return false;
    }
    clear_error();
    return true;
}

template <typename FormatterFactory>
static char* serialize_table(const toml::table& root, FormatterFactory&& formatter_factory) {
    std::ostringstream oss;
    formatter_factory(static_cast<const toml::node&>(root), oss);
    return dup_string_malloc(oss.str());
}

template <typename FormatterFactory>
static std::string format_table(const toml::table& root, FormatterFactory&& formatter_factory) {
    std::ostringstream oss;
    formatter_factory(static_cast<const toml::node&>(root), oss);
    return oss.str();
}

template <typename AssignFunc>
static int assign_with_tokens(const std::shared_ptr<qtoml_document>& doc, const char* path, AssignFunc&& assigner) {
    if (!path || !*path) {
        set_error("QTOML_INVALID_ARGUMENT");
        return 0;
    }
    std::vector<PathToken> tokens;
    if (!parse_path_tokens(path, tokens) || tokens.empty()) {
        set_error("QTOML_INVALID_PATH");
        return 0;
    }
    toml::node* parent = ensure_parent_for_assignment(doc->root, tokens);
    if (!parent) {
        return 0;
    }
    if (assigner(*parent, tokens.back())) {
        clear_error();
        return 1;
    }
    return 0;
}

static int remove_with_tokens(const std::shared_ptr<qtoml_document>& doc, const char* path) {
    if (!path || !*path) {
        set_error("QTOML_INVALID_ARGUMENT");
        return 0;
    }
    std::vector<PathToken> tokens;
    if (!parse_path_tokens(path, tokens) || tokens.empty()) {
        set_error("QTOML_INVALID_PATH");
        return 0;
    }

    toml::node* current = &doc->root;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const PathToken& token = tokens[i];
        bool is_last = (i + 1 == tokens.size());

        if (token.is_index) {
            auto* arr = current->as_array();
            if (!arr) {
                set_error("QTOML_TYPE_MISMATCH");
                return 0;
            }
            if (token.index >= arr->size()) {
                set_error("QTOML_INDEX_OUT_OF_RANGE");
                return 0;
            }

            if (is_last) {
                arr->erase(arr->cbegin() + static_cast<ptrdiff_t>(token.index));
                clear_error();
                return 1;
            }

            toml::node* next = arr->get(token.index);
            if (!next) {
                set_error("QTOML_INDEX_OUT_OF_RANGE");
                return 0;
            }
            current = next;
        } else {
            auto* tbl = current->as_table();
            if (!tbl) {
                set_error("QTOML_TYPE_MISMATCH");
                return 0;
            }

            if (is_last) {
                bool erased = tbl->erase(token.key);
                if (!erased) {
                    set_error("QTOML_NOT_FOUND");
                    return 0;
                }
                clear_error();
                return 1;
            }

            toml::node* next = tbl->get(token.key);
            if (!next) {
                set_error("QTOML_NOT_FOUND");
                return 0;
            }
            current = next;
        }
    }

    set_error("QTOML_INVALID_PATH");
    return 0;
}

} // namespace

extern "C" {

int qtoml_parse_file(const char* path) {
    if (!path) {
        set_error("QTOML_INVALID_PATH");
        return 0;
    }
    clear_error();
    toml::parse_result result = toml::parse_file(path);
    if (!result) {
        set_error(std::string(result.error().description()));
        return 0;
    }
    auto doc = std::make_shared<qtoml_document>();
    doc->root = std::move(result.table());
    int handle = g_next_handle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_doc_mutex);
        g_documents[handle] = std::move(doc);
    }
    clear_error();
    return handle;
}

int qtoml_parse_string(const char* text) {
    if (!text) {
        set_error("QTOML_INVALID_ARGUMENT");
        return 0;
    }
    clear_error();
    toml::parse_result result = toml::parse(std::string_view{text});
    if (!result) {
        set_error(std::string(result.error().description()));
        return 0;
    }
    auto doc = std::make_shared<qtoml_document>();
    doc->root = std::move(result.table());
    int handle = g_next_handle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_doc_mutex);
        g_documents[handle] = std::move(doc);
    }
    clear_error();
    return handle;
}

void qtoml_document_free(int handle) {
    if (handle <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_doc_mutex);
    g_documents.erase(handle);
}

int qtoml_has(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    auto view = view_at_const(doc->root, path);
    if (view) {
        clear_error();
        return 1;
    }
    set_error("QTOML_NOT_FOUND");
    return 0;
}

int qtoml_array_size(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return -1;
    }
    auto view = view_at_const(doc->root, path);
    if (!view) {
        set_error("QTOML_NOT_FOUND");
        return -1;
    }
    if (auto* arr = view.as_array()) {
        clear_error();
        return static_cast<int>(arr->size());
    }
    set_error("QTOML_TYPE_MISMATCH");
    return -1;
}

int qtoml_get_int(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    auto view = view_at_const(doc->root, path);
    if (!view) {
        set_error("QTOML_NOT_FOUND");
        return 0;
    }
    if (auto value = view.value<int64_t>()) {
        if (*value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
            set_error("QTOML_INT_OUT_OF_RANGE");
            return 0;
        }
        clear_error();
        return static_cast<int>(*value);
    }
    set_error("QTOML_TYPE_MISMATCH");
    return 0;
}

double qtoml_get_float(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0.0;
    }
    auto view = view_at_const(doc->root, path);
    if (!view) {
        set_error("QTOML_NOT_FOUND");
        return 0.0;
    }
    if (auto value = view.value<double>()) {
        clear_error();
        return *value;
    }
    set_error("QTOML_TYPE_MISMATCH");
    return 0.0;
}

int qtoml_get_bool(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    auto view = view_at_const(doc->root, path);
    if (!view) {
        set_error("QTOML_NOT_FOUND");
        return 0;
    }
    if (auto value = view.value<bool>()) {
        clear_error();
        return *value ? 1 : 0;
    }
    set_error("QTOML_TYPE_MISMATCH");
    return 0;
}

char* qtoml_get_string(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return dup_empty_malloc();
    }
    auto view = view_at_const(doc->root, path);
    if (!view) {
        set_error("QTOML_NOT_FOUND");
        return dup_empty_malloc();
    }
    if (auto value = view.value<std::string>()) {
        clear_error();
        return dup_string_malloc(*value);
    }
    set_error("QTOML_TYPE_MISMATCH");
    return dup_empty_malloc();
}

int qtoml_set_int(int handle, const char* path, int value) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    return assign_with_tokens(doc, path, [value](toml::node& parent, const PathToken& token) {
        return assign_int64(parent, token, static_cast<int64_t>(value));
    });
}

int qtoml_set_float(int handle, const char* path, double value) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    return assign_with_tokens(doc, path, [value](toml::node& parent, const PathToken& token) {
        return assign_float64(parent, token, value);
    });
}

int qtoml_set_bool(int handle, const char* path, int value) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    bool flag = value != 0;
    return assign_with_tokens(doc, path, [flag](toml::node& parent, const PathToken& token) {
        return assign_bool(parent, token, flag);
    });
}

int qtoml_set_string(int handle, const char* path, const char* value) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    return assign_with_tokens(doc, path, [value](toml::node& parent, const PathToken& token) {
        return assign_string(parent, token, value);
    });
}

int qtoml_remove(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    return remove_with_tokens(doc, path);
}

char* qtoml_serialize_toml(int handle) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return dup_empty_malloc();
    }
    auto formatter_factory = [](const toml::node& node, std::ostringstream& oss) {
        toml::toml_formatter formatter(node);
        oss << formatter;
    };
    clear_error();
    return serialize_table(doc->root, formatter_factory);
}

char* qtoml_serialize_json(int handle) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return dup_empty_malloc();
    }
    auto formatter_factory = [](const toml::node& node, std::ostringstream& oss) {
        toml::json_formatter formatter(node);
        oss << formatter;
    };
    clear_error();
    return serialize_table(doc->root, formatter_factory);
}

char* qtoml_serialize_yaml(int handle) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return dup_empty_malloc();
    }
    auto formatter_factory = [](const toml::node& node, std::ostringstream& oss) {
        toml::yaml_formatter formatter(node);
        oss << formatter;
    };
    clear_error();
    return serialize_table(doc->root, formatter_factory);
}

int qtoml_save_file(int handle, const char* path) {
    auto doc = get_document(handle);
    if (!doc) {
        set_error("QTOML_INVALID_HANDLE");
        return 0;
    }
    auto formatter_factory = [](const toml::node& node, std::ostringstream& oss) {
        toml::toml_formatter formatter(node);
        oss << formatter;
    };
    std::string serialized = format_table(doc->root, formatter_factory);
    return write_file(path, serialized) ? 1 : 0;
}

void qtoml_free_string(char* str) {
    if (str) {
        std::free(str);
    }
}

const char* qtoml_last_error(void) {
    return g_toml_state.last_error.c_str();
}

void qtoml_clear_last_error(void) {
    clear_error();
}

} // extern "C"
