// EXPECT: ligma
// EXPECT: bigma
// EXPECT: true
// EXPECT: 2
int main() {
    var m = ["sigma": "ligma", "ligma": "bigma"];
    var v1 = m["sigma"];
    var v2 = m["ligma"];
    print(v1);
    print(v2);
    print(m.has("sigma"));
    print(m.len());
    ret 0;
}