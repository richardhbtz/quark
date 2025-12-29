// EXPECT: lhs true
// EXPECT: rhs_false
// EXPECT: lhs false
// EXPECT: bool var
// EXPECT: struct direct
// EXPECT: rhs_final
// EXPECT: true
// EXPECT: true
// EXPECT: struct or
// EXPECT: struct or eq

bool side_effect(label: str) {
    print("{}", label);
    ret true;
}

struct Flags {
    data {
        value: bool
    }
}

int main() {
    if (true || side_effect("rhs_skip")) {
        print("{}", "lhs true");
    }

    if (false || side_effect("rhs_false")) {
        print("{}", "lhs false");
    }

    var directVar: bool = true;
    if (directVar) {
        print("{}", "bool var");
    }

    var enabled = Flags { value: true };
    if (enabled.value) {
        print("{}", "struct direct");
    }

    var disabled = Flags { value: false };
    var should_link = disabled.value || side_effect("rhs_final");
    print("{}", should_link);
    print("{}", should_link == true);
    if (should_link) {
        print("{}", "struct or");
    }

    if (should_link == true) {
        print("{}", "struct or eq");
    }

    ret 0;
}
