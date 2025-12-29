// EXPECT: hello world
// EXPECT: 5
// EXPECT: 11
// EXPECT: true
// EXPECT: he
// EXPECT: llo
int main() {
    var a: str = "hello";
    var b: str = " world";
    var c = a + b;
    print("{}", c);
    print("{}", a.length());
    print("{}", c.length());
    print("{}", c.find("ll"));
    print("{}", a.slice(0, 2));
    print("{}", a.slice(2, 5));
    ret 0;
}
