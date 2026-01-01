// EXPECT: true
// EXPECT: 2
// EXPECT: 2
// EXPECT: 1
int main() {
    var m = map_new();
    map_set(m, "a", "1");
    map_set(m, "b", "2");

    print(map_has(m, "a"));
    print(map_get(m, "b"));
    print(map_len(m));

    map_remove(m, "a");
    print(map_len(m));

    map_free(m);
    ret 0;
}


