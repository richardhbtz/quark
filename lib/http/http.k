extern "C" {
    str http_get(str);
    str http_post(str, str, str);
    void http_free(str);
    void http_cleanup();
    // Extended API
    str http_request_ex(str, str, str, str, str[], int, int); // method, url, body, content_type, headers, header_count, timeout
    int http_last_status();
    str http_last_error();
    void http_clear_last_error();
    void http_set_max_response_size(int);
}

// Namespaced wrapper to avoid global get()/post() conflicts
struct Http {
    data
    {

    }

    Http new() {
        ret Http {};
    }
    
    str get(url: str) {
        ret http_get(url);
    }

    str post(url: str, body: str, content_type: str) {
        ret http_post(url, body, content_type);
    }

    // Generic request with advanced options.
    // headers: array where each element is already "Name: value"; timeout seconds (<=0 uses default)
    str request(method: str, url: str, body: str, content_type: str, headers: str[], timeout: int) {
        ret http_request_ex(method, url, body, content_type, headers, headers.length(), timeout);
    }

    str get_with(url: str, headers: str[], timeout: int) {
        ret http_request_ex("GET", url, "", "", headers, headers.length(), timeout);
    }

    str post_with(url: str, body: str, content_type: str, headers: str[], timeout: int) {
        ret http_request_ex("POST", url, body, content_type, headers, headers.length(), timeout);
    }

    int status() {
        ret http_last_status();
    }

    str last_error() {
        ret http_last_error();
    }

    void clear_error() {
        http_clear_last_error();
    }

    void set_max_response_size(bytes: int) {
        http_set_max_response_size(bytes);
    }

    void free_response(response: str) {
        http_free(response);
    }

    void cleanup() {
        http_cleanup();
    }
}

// Example usage (advanced):
// var http = Http.new();
// var headers: str[] = { "Accept: application/json", "User-Agent: QuarkClient/1.0" };
// var body: str = http.get_with("https://httpbin.org/get", headers, 10);
// print("Status {} BodyLen {}", http.status(), body.length());
