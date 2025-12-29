#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum QuarkCompileStatus {
    QUARK_COMPILE_OK = 0,
    QUARK_COMPILE_ERR_INVALID_ARGUMENT = -1,
    QUARK_COMPILE_ERR_IO = -2,
    QUARK_COMPILE_ERR_COMPILATION = -3,
    QUARK_COMPILE_ERR_INTERNAL = -4
} QuarkCompileStatus;

typedef enum QuarkLogLevel {
    QUARK_LOG_DEBUG = 0,
    QUARK_LOG_INFO = 1,
    QUARK_LOG_WARNING = 2,
    QUARK_LOG_ERROR = 3,
    QUARK_LOG_SUCCESS = 4,
    QUARK_LOG_PROGRESS = 5
} QuarkLogLevel;

typedef enum QuarkVerbosityLevel {
    QUARK_VERBOSITY_QUIET = 0,
    QUARK_VERBOSITY_NORMAL = 1,
    QUARK_VERBOSITY_VERBOSE = 2,
    QUARK_VERBOSITY_DEBUG = 3
} QuarkVerbosityLevel;

typedef struct QuarkCompilerOptions {
    const char* input_path;
    const char* output_path;
    int optimize;
    int optimization_level;
    int freestanding;
    int emit_llvm;
    int emit_asm;
    int verbosity;
    int color_output;
    const char* const* library_paths;
    size_t library_path_count;
    const char* const* link_libraries;
    size_t link_library_count;
    int use_cache;
    int clear_cache;
    const char* cache_dir;
} QuarkCompilerOptions;

typedef struct QuarkCompilerHandle QuarkCompilerHandle;

typedef void (*QuarkDiagnosticCallback)(QuarkLogLevel level, const char* message, int newline, void* user_data);

typedef void (*QuarkRawOutputCallback)(const char* text, int newline, void* user_data);

QuarkCompilerHandle* quark_compiler_create(void);
void quark_compiler_destroy(QuarkCompilerHandle* handle);

void quark_compiler_set_diagnostic_callback(QuarkCompilerHandle* handle,
                                            QuarkDiagnosticCallback callback,
                                            void* user_data);
void quark_compiler_set_raw_output_callback(QuarkCompilerHandle* handle,
                                            QuarkRawOutputCallback callback,
                                            void* user_data);
void quark_compiler_set_console_echo(QuarkCompilerHandle* handle, int enabled);

int quark_compiler_compile_file(QuarkCompilerHandle* handle,
                                const QuarkCompilerOptions* options);
int quark_compiler_compile_source(QuarkCompilerHandle* handle,
                                  const char* source_text,
                                  const char* virtual_filename,
                                  const QuarkCompilerOptions* options);

int quark_compiler_get_error_count(const QuarkCompilerHandle* handle);
int quark_compiler_get_warning_count(const QuarkCompilerHandle* handle);

/* Convenience helpers for direct CLI integrations. */
int quark_cli_compile_file(const char* input_path,
                           const char* output_path,
                           int optimize,
                           int optimization_level,
                           int freestanding,
                           int emit_llvm,
                           int emit_asm,
                           int verbosity,
                           int color_output);

int quark_cli_last_error_count(void);
int quark_cli_last_warning_count(void);
const char* quark_cli_default_output(void);

#ifdef __cplusplus
}
#endif
