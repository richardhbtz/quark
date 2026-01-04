#include <cstdlib>
#include <cstring>
#include <cstdint>

extern "C" {

// =====================================================
// Map Implementation
// =====================================================

struct QuarkMapEntry {
    char* key;
    void* value;
    uint32_t hash;
    bool occupied;
};

struct QuarkMap {
    QuarkMapEntry* entries;
    int32_t size;
    int32_t capacity;
};

static uint32_t quark_hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static void quark_map_resize(QuarkMap* m, int32_t newCapacity) {
    QuarkMapEntry* oldEntries = m->entries;
    int32_t oldCapacity = m->capacity;
    
    m->entries = (QuarkMapEntry*)calloc(newCapacity, sizeof(QuarkMapEntry));
    m->capacity = newCapacity;
    m->size = 0;
    
    for (int32_t i = 0; i < oldCapacity; i++) {
        if (oldEntries[i].occupied) {
            // Reinsert
            uint32_t hash = oldEntries[i].hash;
            int32_t idx = hash % newCapacity;
            while (m->entries[idx].occupied) {
                idx = (idx + 1) % newCapacity;
            }
            m->entries[idx].key = oldEntries[i].key;
            m->entries[idx].value = oldEntries[i].value;
            m->entries[idx].hash = hash;
            m->entries[idx].occupied = true;
            m->size++;
        }
    }
    
    free(oldEntries);
}

void* __quark_map_new() {
    QuarkMap* m = (QuarkMap*)malloc(sizeof(QuarkMap));
    m->capacity = 16;
    m->size = 0;
    m->entries = (QuarkMapEntry*)calloc(m->capacity, sizeof(QuarkMapEntry));
    return m;
}

void __quark_map_free(void* mapPtr) {
    if (!mapPtr) return;
    QuarkMap* m = (QuarkMap*)mapPtr;
    for (int32_t i = 0; i < m->capacity; i++) {
        if (m->entries[i].occupied && m->entries[i].key) {
            free(m->entries[i].key);
        }
    }
    free(m->entries);
    free(m);
}

void __quark_map_set(void* mapPtr, const char* key, void* value) {
    if (!mapPtr || !key) return;
    QuarkMap* m = (QuarkMap*)mapPtr;
    
    // Resize if load factor > 0.75
    if (m->size * 4 >= m->capacity * 3) {
        quark_map_resize(m, m->capacity * 2);
    }
    
    uint32_t hash = quark_hash_string(key);
    int32_t idx = hash % m->capacity;
    
    // Linear probing
    while (m->entries[idx].occupied) {
        if (m->entries[idx].hash == hash && strcmp(m->entries[idx].key, key) == 0) {
            // Update existing
            m->entries[idx].value = value;
            return;
        }
        idx = (idx + 1) % m->capacity;
    }
    
    // Insert new
    m->entries[idx].key = strdup(key);
    m->entries[idx].value = value;
    m->entries[idx].hash = hash;
    m->entries[idx].occupied = true;
    m->size++;
}

void* __quark_map_get(void* mapPtr, const char* key) {
    if (!mapPtr || !key) return nullptr;
    QuarkMap* m = (QuarkMap*)mapPtr;
    
    uint32_t hash = quark_hash_string(key);
    int32_t idx = hash % m->capacity;
    int32_t startIdx = idx;
    
    do {
        if (!m->entries[idx].occupied) {
            return nullptr;
        }
        if (m->entries[idx].hash == hash && strcmp(m->entries[idx].key, key) == 0) {
            return m->entries[idx].value;
        }
        idx = (idx + 1) % m->capacity;
    } while (idx != startIdx);
    
    return nullptr;
}

bool __quark_map_contains(void* mapPtr, const char* key) {
    if (!mapPtr || !key) return false;
    QuarkMap* m = (QuarkMap*)mapPtr;
    
    uint32_t hash = quark_hash_string(key);
    int32_t idx = hash % m->capacity;
    int32_t startIdx = idx;
    
    do {
        if (!m->entries[idx].occupied) {
            return false;
        }
        if (m->entries[idx].hash == hash && strcmp(m->entries[idx].key, key) == 0) {
            return true;
        }
        idx = (idx + 1) % m->capacity;
    } while (idx != startIdx);
    
    return false;
}

bool __quark_map_remove(void* mapPtr, const char* key) {
    if (!mapPtr || !key) return false;
    QuarkMap* m = (QuarkMap*)mapPtr;
    
    uint32_t hash = quark_hash_string(key);
    int32_t idx = hash % m->capacity;
    int32_t startIdx = idx;
    
    do {
        if (!m->entries[idx].occupied) {
            return false;
        }
        if (m->entries[idx].hash == hash && strcmp(m->entries[idx].key, key) == 0) {
            // Found - mark as deleted (tombstone)
            free(m->entries[idx].key);
            m->entries[idx].key = nullptr;
            m->entries[idx].value = nullptr;
            m->entries[idx].occupied = false;
            m->size--;
            return true;
        }
        idx = (idx + 1) % m->capacity;
    } while (idx != startIdx);
    
    return false;
}

int32_t __quark_map_len(void* mapPtr) {
    if (!mapPtr) return 0;
    QuarkMap* m = (QuarkMap*)mapPtr;
    return m->size;
}

// =====================================================
// Native List Implementation (Dynamic Array)
// =====================================================

struct QuarkList {
    void** data;
    int32_t size;
    int32_t capacity;
};

void* __quark_list_new() {
    QuarkList* l = (QuarkList*)malloc(sizeof(QuarkList));
    l->capacity = 8;
    l->size = 0;
    l->data = (void**)malloc(l->capacity * sizeof(void*));
    return l;
}

void __quark_list_free(void* listPtr) {
    if (!listPtr) return;
    QuarkList* l = (QuarkList*)listPtr;
    free(l->data);
    free(l);
}

void __quark_list_push(void* listPtr, void* value) {
    if (!listPtr) return;
    QuarkList* l = (QuarkList*)listPtr;
    
    // Resize if needed
    if (l->size >= l->capacity) {
        l->capacity *= 2;
        l->data = (void**)realloc(l->data, l->capacity * sizeof(void*));
    }
    
    l->data[l->size++] = value;
}

void* __quark_list_get(void* listPtr, int32_t index) {
    if (!listPtr) return nullptr;
    QuarkList* l = (QuarkList*)listPtr;
    
    if (index < 0 || index >= l->size) {
        return nullptr; // Out of bounds
    }
    
    return l->data[index];
}

void __quark_list_set(void* listPtr, int32_t index, void* value) {
    if (!listPtr) return;
    QuarkList* l = (QuarkList*)listPtr;
    
    if (index < 0 || index >= l->size) {
        return; // Out of bounds
    }
    
    l->data[index] = value;
}

void __quark_list_remove(void* listPtr, int32_t index) {
    if (!listPtr) return;
    QuarkList* l = (QuarkList*)listPtr;
    
    if (index < 0 || index >= l->size) {
        return; // Out of bounds
    }
    
    // Shift elements
    for (int32_t i = index; i < l->size - 1; i++) {
        l->data[i] = l->data[i + 1];
    }
    l->size--;
}

int32_t __quark_list_len(void* listPtr) {
    if (!listPtr) return 0;
    QuarkList* l = (QuarkList*)listPtr;
    return l->size;
}

} // extern "C"
