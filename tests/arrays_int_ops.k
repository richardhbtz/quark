// EXPECT: 3
// EXPECT: 4
// EXPECT: 99
// EXPECT: 3
// EXPECT: 2
// EXPECT: 99
int main() {
    var arr = [1,2,3];
    print("{}", arr.length());

    var x = 99;
    arr = arr.push(&x, 4);
    print("{}", arr.length());
    print("{}", arr[3]);

    var s = arr.slice(1, 4, 4);
    print("{}", s.length());
    print("{}", s[0]);
    print("{}", s[2]);

    arr.free();
    s.free();
    ret 0;
}
