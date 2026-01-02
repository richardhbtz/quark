module toml

extern "C" {
    int qtoml_parse_file(str);
    int qtoml_parse_string(str);
    void qtoml_document_free(int);

    int qtoml_has(int, str);
    int qtoml_array_size(int, str);

    int qtoml_get_int(int, str);
    double qtoml_get_float(int, str);
    int qtoml_get_bool(int, str);
    str qtoml_get_string(int, str);

    int qtoml_set_int(int, str, int);
    int qtoml_set_float(int, str, double);
    int qtoml_set_bool(int, str, int);
    int qtoml_set_string(int, str, str);

    int qtoml_remove(int, str);

    str qtoml_serialize_toml(int);
    str qtoml_serialize_json(int);
    str qtoml_serialize_yaml(int);

    int qtoml_save_file(int, str);

    void qtoml_free_string(str);

    str qtoml_last_error();
    void qtoml_clear_last_error();
}

struct Toml {
    data { handle: int }

    Toml new() {
        var zero = 0;
        ret Toml { handle: zero };
    }

    bool load_file(path: str) {
        var h = qtoml_parse_file(path);
        var success = h != 0;
        if (success) {
            if (this.handle != 0) { qtoml_document_free(this.handle); }
            this.handle = h;
        }
        ret success;
    }

    bool load_string(text: str) {
        var h = qtoml_parse_string(text);
        var success = h != 0;
        if (success) {
            if (this.handle != 0) { qtoml_document_free(this.handle); }
            this.handle = h;
        }
        ret success;
    }

    bool is_valid() { ret this.handle != 0; }

    void destroy() { if (this.handle != 0) { qtoml_document_free(this.handle); this.handle = 0; } }
    bool has(path: str) { ret qtoml_has(this.handle, path) != 0; }
    int array_size(path: str) { ret qtoml_array_size(this.handle, path); }
    int get_int(path: str) { ret qtoml_get_int(this.handle, path); }
    double get_float(path: str) { ret qtoml_get_float(this.handle, path); }
    bool get_bool(path: str) { ret qtoml_get_bool(this.handle, path) != 0; }
    str get_string(path: str) { ret qtoml_get_string(this.handle, path); }

    bool set_int(path: str, value: int) { ret qtoml_set_int(this.handle, path, value) != 0; }
    bool set_float(path: str, value: double) { ret qtoml_set_float(this.handle, path, value) != 0; }
    bool set_bool(path: str, value: bool) {
        var flag = 0;
        if (value) { flag = 1; }
        ret qtoml_set_bool(this.handle, path, flag) != 0;
    }
    bool set_string(path: str, value: str) { ret qtoml_set_string(this.handle, path, value) != 0; }

    bool remove(path: str) { ret qtoml_remove(this.handle, path) != 0; }

    str to_toml() { ret qtoml_serialize_toml(this.handle); }
    str to_json() { ret qtoml_serialize_json(this.handle); }
    str to_yaml() { ret qtoml_serialize_yaml(this.handle); }

    bool save_file(path: str) { ret qtoml_save_file(this.handle, path) != 0; }

    void free_string(value: str) { qtoml_free_string(value); }
    str last_error() { ret qtoml_last_error(); }
    void clear_error() { qtoml_clear_last_error(); }
}
