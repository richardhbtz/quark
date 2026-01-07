// EXPECT: Alice
// EXPECT: admin
// EXPECT: true
// EXPECT: 2
int main() {
    map m = { "name": "Alice", "role": "admin" };
    var v1 = m["name"];
    var v2 = m["role"];
    print(v1);
    print(v2);
    print(m.contains("name"));
    print(m.len());
    ret 0;
}