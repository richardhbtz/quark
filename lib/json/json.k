module json

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
    data { 
        content: str;
    }

    Json new(text: str) { ret Json { content: text }; }

    str minify() { ret qjson_minify(this.content); }
    str pretty(indent: str, newline: str) { ret qjson_pretty(this.content, indent, newline); }
    bool validate() { ret qjson_validate(this.content) != 0; }
    str get(path: str) { ret qjson_get(this.content, path); }
    str get_string(path: str) { ret qjson_get_string(this.content, path); }
    
    // Array access methods
    int get_array_length(path: str) { ret qjson_get_array_length(this.content, path); }
    str get_array_item(path: str, index: int) { ret qjson_get_array_item(this.content, path, index); }
    str get_array_string(path: str, index: int) { ret qjson_get_array_string(this.content, path, index); }

    str last_error() { ret qjson_last_error(); }
    int last_error_code() { ret qjson_last_error_code(); }
    int last_error_offset() { ret qjson_last_error_offset(); }
    int last_error_line() { ret qjson_last_error_line(); }
    int last_error_row() { ret qjson_last_error_row(); }
    void clear_error() { qjson_clear_last_error(); }
    void free(s: str) { qjson_free(s); }
}
