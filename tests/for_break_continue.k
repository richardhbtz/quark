//EXPECT: 1
//EXPECT: 3
//EXPECT: 5
//EXPECT: 7
//EXPECT: 9
int main() {
    var i: int = 0;
    // range-based for equivalent: emulate with for-style sugar
    for (i in 0..10) {
        if ((i % 2) == 0) { continue; }
        print("{}", i);
        if (i == 9) { break; }
    }
    ret 0;
}
