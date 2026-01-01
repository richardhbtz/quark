extern "C" {
    str qjson_minify(str);
    str qjson_pretty(str, str, str);
    int qjson_validate(str);
    str qjson_get(str, str);
    str qjson_get_string(str, str);
    int qjson_get_array_length(str, str);
    str qjson_get_array_item(str, str, int);
    str qjson_get_array_string(str, str, int);
    str qjson_last_error();
    int qjson_last_error_code();
    int qjson_last_error_offset();
    int qjson_last_error_line();
    int qjson_last_error_row();
    void qjson_clear_last_error();
    void qjson_free(str);
}

struct Json {
    data { }

    Json new() { ret Json {}; }

    str minify(text: str) { ret qjson_minify(text); }
    str pretty(text: str, indent: str, newline: str) { ret qjson_pretty(text, indent, newline); }
    bool validate(text: str) { ret qjson_validate(text) != 0; }
    str get(text: str, path: str) { ret qjson_get(text, path); }
    str get_string(text: str, path: str) { ret qjson_get_string(text, path); }
    
    // Array access methods
    int get_array_length(text: str, path: str) { ret qjson_get_array_length(text, path); }
    str get_array_item(text: str, path: str, index: int) { ret qjson_get_array_item(text, path, index); }
    str get_array_string(text: str, path: str, index: int) { ret qjson_get_array_string(text, path, index); }

    str last_error() { ret qjson_last_error(); }
    int last_error_code() { ret qjson_last_error_code(); }
    int last_error_offset() { ret qjson_last_error_offset(); }
    int last_error_line() { ret qjson_last_error_line(); }
    int last_error_row() { ret qjson_last_error_row(); }
    void clear_error() { qjson_clear_last_error(); }
    void free(s: str) { qjson_free(s); }
}
