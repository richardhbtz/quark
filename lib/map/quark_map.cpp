#include <unordered_map>
#include <string>
#include <memory>

extern "C" {

struct quark_map {
    std::unordered_map<std::string, std::string> data;
};

quark_map* quark_map_new() {
    return new quark_map();
}

void quark_map_free(quark_map* m) {
    if (!m) return;
    delete m;
}

void quark_map_set(quark_map* m, const char* key, const char* value) {
    if (!m || !key || !value) return;
    m->data[key] = value;
}

const char* quark_map_get(quark_map* m, const char* key) {
    if (!m || !key) return nullptr;
    auto it = m->data.find(key);
    if (it == m->data.end()) return nullptr;
    return it->second.c_str();
}

int quark_map_has(quark_map* m, const char* key) {
    if (!m || !key) return 0;
    return m->data.find(key) != m->data.end();
}

int quark_map_len(quark_map* m) {
    if (!m) return 0;
    return static_cast<int>(m->data.size());
}

int quark_map_remove(quark_map* m, const char* key) {
    if (!m || !key) return 0;
    return static_cast<int>(m->data.erase(key) > 0 ? 1 : 0);
}

} // extern "C"
