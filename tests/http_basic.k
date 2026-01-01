import "lib/http/http.k"

//EXPECT: Hello world!
int main() {
    var http = Http.new();
    var i: str = http.get("https://httpbin.org/base64/SGVsbG8gd29ybGQh");

    http.cleanup();

    print("{}", i);

    ret 0;
}
