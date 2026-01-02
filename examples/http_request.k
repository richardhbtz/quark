// HTTP Requests using the built-in HTTP library

import http

int main() {
    // Create HTTP client
    var http = Http.new();

    // Make a GET request
    var response = http.get("https://httpbin.org/get");
    print("{}", "GET Response:");
    print("{}", response);

    // Make a request to a JSON endpoint
    var json_response = http.get("https://httpbin.org/json");
    print("{}", "JSON Response:");
    print("{}", json_response);

    // Clean up resources
    http.cleanup();

    ret 0;
}
