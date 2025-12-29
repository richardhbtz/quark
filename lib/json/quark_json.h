#pragma once

#ifdef __cplusplus
extern "C" {
#endif

char* qjson_minify(const char* text);
char* qjson_pretty(const char* text, const char* indent, const char* newline);
int   qjson_validate(const char* text);
char* qjson_get(const char* text, const char* path);
char* qjson_get_string(const char* text, const char* path);

const char* qjson_last_error();
int   qjson_last_error_code();
int   qjson_last_error_offset();
int   qjson_last_error_line();
int   qjson_last_error_row();
void  qjson_clear_last_error();

void  qjson_free(char* ptr);

#ifdef __cplusplus
}
#endif
