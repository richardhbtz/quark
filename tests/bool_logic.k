// EXPECT: true
// EXPECT: false
// EXPECT: true
// EXPECT: false
int main() {
    var t: bool = true;
    var f: bool = false;
    print("{}", t && (true || false));
    print("{}", f && t);
    print("{}", (3 < 5) && (5 >= 5));
    print("{}", (3 == 4) || (1 > 2));
    ret 0;
}
