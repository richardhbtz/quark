// EXPECT: 42
// EXPECT: 7
// EXPECT: 1
// EXPECT: 0
int main() {
    var s: str = "42";
    print("{}", to_int(s));
    print("{}", to_int(7));
    print("{}", to_int(true));
    print("{}", to_int(false));
    ret 0;
}
