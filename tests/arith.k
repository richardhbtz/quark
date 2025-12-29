// EXPECT: 7
// EXPECT: 3
// EXPECT: 10
// EXPECT: 2
// EXPECT: 1
int main() {
    var a: int = 5;
    var b: int = 2;
    print("{}", a + b);
    print("{}", a - b);
    print("{}", a * b);
    print("{}", a / b);
    print("{}", a % b);
    // Boolean printing varies; keep test purely arithmetic
    ret 0;
}
