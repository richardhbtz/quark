//EXPECT: 3
//EXPECT: 5
//EXPECT: done
int main() {
    var a: int = 0;
    // while(true) with break after 3
    while (true) {
        a = a + 1;
        if (a == 3) { break; }
    }
    print("{}", a);

    // while(1==1) constant-folded; continue to skip even numbers; break at 5
    var b: int = 0;
    var i: int = 0;
    while (1 == 1) {
        i = i + 1;
        if ((i % 2) == 0) { continue; }
        b = i;
        if (b == 5) { break; }
    }
    print("{}", b);

    // while(1==2) should not execute
    var c: int = 0;
    while (1 == 2) { c = 42; }
    if (c == 0) { print("{}", "done"); }

    ret 0;
}
