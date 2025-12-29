extern "C" {
    str io_read_file(str);
    int io_write_file(str, str);
    int io_append_file(str, str);
    str io_read_stdin_line();
    str io_read_stdin_all();
    int io_stdout_write(str);
    int io_stderr_write(str);
    int io_stdout_flush();
    int io_stderr_flush();
    void io_free(str);
    str io_last_error();
    void io_clear_last_error();
}

struct IO {
    data {}

    IO new() {
        ret IO {};
    }

    str read_file(path: str) {
        ret io_read_file(path);
    }

    bool write_file(path: str, dt: str) {
        ret io_write_file(path, dt) != 0;
    }

    bool append_file(path: str, dt: str) {
        ret io_append_file(path, dt) != 0;
    }

    str read_stdin_line() {
        ret io_read_stdin_line();
    }

    str read_stdin_all() {
        ret io_read_stdin_all();
    }

    bool stdout_write(dt: str) {
        ret io_stdout_write(dt) != 0;
    }

    bool stderr_write(dt: str) {
        ret io_stderr_write(dt) != 0;
    }

    bool stdout_flush() {
        ret io_stdout_flush() != 0;
    }

    bool stderr_flush() {
        ret io_stderr_flush() != 0;
    }

    str last_error() {
        ret io_last_error();
    }

    void clear_error() {
        io_clear_last_error();
    }

    void free(buffer: str) {
        io_free(buffer);
    }
}
