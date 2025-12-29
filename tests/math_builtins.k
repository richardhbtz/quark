// Test math builtins

int main() {
    print(abs_i32(-5));
    print(abs_f64(-3.5));
    print(min(3, 7));
    print(max(3, 7));
    print(min(3.0, 7.5));
    print(max(3.0, 7.5));
    print(clamp(5, 0, 3));
    print(clamp(-2, 0, 3));
    print(clamp(10, 0, 3));
    print(clamp(2.5, 0.0, 2.0));
}

// EXPECT: 5
// EXPECT: 3.500000
// EXPECT: 3
// EXPECT: 7
// EXPECT: 3.000000
// EXPECT: 7.500000
// EXPECT: 3
// EXPECT: 0
// EXPECT: 3
// EXPECT: 2.000000
