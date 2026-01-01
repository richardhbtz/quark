// EXPECT: Base::hello
// EXPECT: Child::only
// Base defines hello(); Child doesn't override it. Dispatch should find Base::hello.
struct Base {
    data {
        x: int
    }

    Base new(x: int) {
        ret Base { x: x };
    }

    str hello() {
        ret "Base::hello";
    }
}

struct Child : Base {
    data {
        y: int
    }

    Child new(x: int, y: int) {
        ret Child { x: x, y: y };
    }

    str only() {
        ret "Child::only";
    }
}

int main() {
    var c = Child.new(1, 2);
    print("{}", c.hello());
    print("{}", c.only());
    ret 0;
}
