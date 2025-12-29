#include "quark_io.h"

#include <fstream>
#include <sstream>
#include <string>
#include <iterator>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>

namespace {

struct IoThreadState {
    std::string last_error;
};

thread_local IoThreadState g_io_state;

static void set_error(const std::string& message) {
    g_io_state.last_error = message;
}

static void clear_error() {
    g_io_state.last_error.clear();
}

static char* dup_empty_malloc() {
    char* s = static_cast<char*>(std::malloc(1));
    if (s) {
        s[0] = '\0';
    }
    return s;
}

static char* dup_string_malloc(const std::string& value) {
    char* buffer = static_cast<char*>(std::malloc(value.size() + 1));
    if (!buffer) {
        set_error("IO_ALLOC_FAILED");
        return dup_empty_malloc();
    }
    std::memcpy(buffer, value.data(), value.size());
    buffer[value.size()] = '\0';
    return buffer;
}

static std::string read_stream_into_string(std::istream& stream) {
    std::ostringstream oss;
    oss << stream.rdbuf();
    return oss.str();
}

static bool write_to_file(const char* path, const char* data, std::ios::openmode mode) {
    if (!path || !data) {
        set_error("IO_INVALID_ARGUMENT");
        return false;
    }

    std::ofstream file(path, std::ios::binary | mode);
    if (!file.is_open()) {
        set_error("IO_OPEN_FAILED");
        return false;
    }

    const auto length = std::strlen(data);
    file.write(data, static_cast<std::streamsize>(length));
    if (!file) {
        set_error("IO_WRITE_FAILED");
        return false;
    }

    clear_error();
    return true;
}

} // namespace

extern "C" {

char* io_read_file(const char* path) {
    if (!path) {
        set_error("IO_INVALID_PATH");
        return dup_empty_malloc();
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        set_error("IO_OPEN_FAILED");
        return dup_empty_malloc();
    }

    std::string contents = read_stream_into_string(file);
    if (file.bad()) {
        set_error("IO_READ_FAILED");
        return dup_empty_malloc();
    }

    clear_error();
    return dup_string_malloc(contents);
}

int io_write_file(const char* path, const char* data) {
    return write_to_file(path, data, std::ios::trunc) ? 1 : 0;
}

int io_append_file(const char* path, const char* data) {
    return write_to_file(path, data, std::ios::app) ? 1 : 0;
}

char* io_read_stdin_line(void) {
    std::string line;
    if (!std::getline(std::cin, line)) {
        if (std::cin.eof()) {
            set_error("IO_EOF");
        } else {
            set_error("IO_STDIN_READ_FAILED");
        }
        return dup_empty_malloc();
    }

    clear_error();
    return dup_string_malloc(line);
}

char* io_read_stdin_all(void) {
    std::istreambuf_iterator<char> begin(std::cin.rdbuf());
    std::istreambuf_iterator<char> end;
    std::string data(begin, end);

    if (std::cin.bad()) {
        set_error("IO_STDIN_READ_FAILED");
        return dup_empty_malloc();
    }

    clear_error();
    return dup_string_malloc(data);
}

int io_stdout_write(const char* data) {
    if (!data) {
        set_error("IO_INVALID_ARGUMENT");
        return 0;
    }

    const size_t length = std::strlen(data);
    if (length == 0) {
        clear_error();
        return 1;
    }

    const size_t written = std::fwrite(data, 1, length, stdout);
    if (written != length) {
        set_error("IO_STDOUT_WRITE_FAILED");
        return 0;
    }

    clear_error();
    return 1;
}

int io_stderr_write(const char* data) {
    if (!data) {
        set_error("IO_INVALID_ARGUMENT");
        return 0;
    }

    const size_t length = std::strlen(data);
    if (length == 0) {
        clear_error();
        return 1;
    }

    const size_t written = std::fwrite(data, 1, length, stderr);
    if (written != length) {
        set_error("IO_STDERR_WRITE_FAILED");
        return 0;
    }

    clear_error();
    return 1;
}

int io_stdout_flush(void) {
    if (std::fflush(stdout) != 0) {
        set_error("IO_STDOUT_FLUSH_FAILED");
        return 0;
    }

    clear_error();
    return 1;
}

int io_stderr_flush(void) {
    if (std::fflush(stderr) != 0) {
        set_error("IO_STDERR_FLUSH_FAILED");
        return 0;
    }

    clear_error();
    return 1;
}

void io_free(void* buffer) {
    if (buffer) {
        std::free(buffer);
    }
}

const char* io_last_error(void) {
    return g_io_state.last_error.c_str();
}

void io_clear_last_error(void) {
    clear_error();
}

} // extern "C"
