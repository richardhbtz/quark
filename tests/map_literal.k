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
    print(map_has(m, "sigma"));
    print(map_len(m));
    ret 0;
}