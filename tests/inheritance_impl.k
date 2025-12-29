// EXPECT: base
// EXPECT: child
// EXPECT: child
// EXPECT: base
struct Base {
    data { name: str }

    extend str who() {
        ret "base";
    }

    str call_who() { ret this.who(); }
}

struct Child : Base {
    data { id: int }

    impl str who() {
        ret "child";
    }
}

int main() {
    var b = Base { name: "B" };
    var c = Child { name: "C", id: 1 };
    print("{}", b.who());
    print("{}", c.who());
    print("{}", c.call_who());
    print("{}", b.call_who());
    ret 0;
}
