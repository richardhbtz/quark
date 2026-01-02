// Test module declaration syntax
module my_module

int add(a: int, b: int) {
    ret a + b;
}

int main() {
    var result = add(3, 4);
    print("{}", format("Result: {}", to_string(result)));
    ret 0;
}

// EXPECT: Result: 7
