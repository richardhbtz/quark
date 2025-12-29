// EXPECT: base
// EXPECT: child-ex
// EXPECT: child-ex
// EXPECT: base
struct BaseEx {
    data { name: str }

    extend str who() {
        ret "base";
    }

    str call_who() { ret this.who(); }
}

struct ChildEx : BaseEx {
    data { id: int }

    extend str who() {
        ret "child-ex";
    }
}

int main() {
    var b = BaseEx { name: "B" };
    var c = ChildEx { name: "C", id: 1 };
    print("{}", b.who());
    print("{}", c.who());
    print("{}", c.call_who());
    print("{}", b.call_who());
    ret 0;
}
