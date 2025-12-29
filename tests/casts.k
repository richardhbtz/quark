// EXPECT: 3
// EXPECT: 2.000000
// EXPECT: 4.500000
// EXPECT: 9.000000
// EXPECT: false
// EXPECT: true
// EXPECT: 1
// EXPECT: 0

int main() {
    var a: double = 3.9;
    print((int)a);            // 3

    var b: int = 2;
    print((float)b);          // 2.000000

    var c: float = 4.5;
    print((double)c);         // 4.500000

    var d: double = 9.0;
    print((double)d);         // 9.000000

    // bool casts
    print((bool)0);           // false
    print((bool)1);           // true
    print((int)true);         // 1
    print((int)false);        // 0

    ret 0;
}
