// EXPECT: 42
// EXPECT: true
// EXPECT: 3.140000
// EXPECT: hello
int main() {
    var i: int = 42;
    var b: bool = true;
    var d: double = 3.14;
    var s: str = "hello";
    print("{}", to_string(i));
    print("{}", to_string(b));
    print("{}", to_string(d));
    print("{}", to_string(s));
    ret 0;
}
