#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int qtoml_parse_file(const char* path);
int qtoml_parse_string(const char* text);
void qtoml_document_free(int handle);

int qtoml_has(int handle, const char* path);
int qtoml_array_size(int handle, const char* path);

int qtoml_get_int(int handle, const char* path);
double qtoml_get_float(int handle, const char* path);
int qtoml_get_bool(int handle, const char* path);
char* qtoml_get_string(int handle, const char* path);

int qtoml_set_int(int handle, const char* path, int value);
int qtoml_set_float(int handle, const char* path, double value);
int qtoml_set_bool(int handle, const char* path, int value);
int qtoml_set_string(int handle, const char* path, const char* value);

int qtoml_remove(int handle, const char* path);

char* qtoml_serialize_toml(int handle);
char* qtoml_serialize_json(int handle);
char* qtoml_serialize_yaml(int handle);

int qtoml_save_file(int handle, const char* path);

void qtoml_free_string(char* str);

const char* qtoml_last_error(void);
void qtoml_clear_last_error(void);

#ifdef __cplusplus
}
#endif
