// EXPECT: 3
// EXPECT: 4
// EXPECT: true
// EXPECT: 3
// EXPECT: false
// EXPECT: true
int main() {
    var arr = [true,false,true];
    print("{}", arr.length());

    var b = true;
    arr = arr.push(&b, 1);
    print("{}", arr.length());
    print("{}", arr[3]);

    var s = arr.slice(0, 3, 1);
    print("{}", s.length());
    print("{}", s[1]);
    print("{}", s[2]);

    arr.free();
    s.free();
    ret 0;
}
