// EXPECT: true
// EXPECT: 2
// EXPECT: 2
// EXPECT: 1
int main() {
    var m = map_new();
    m.set("a", "1");
    m.set("b", "2");

    print(m.has("a"));
    print(m.get("b"));
    print(m.len());

    m.remove("a");
    print(m.len());

    m.free();
    ret 0;
}


