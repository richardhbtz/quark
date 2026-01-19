// EXPECT: x: 1.500000
// EXPECT: y: 2
// EXPECT: z: 3.500000
int main() {
    // Implicitly create variables via assignment
    x = 1.5;      // should infer double or float? Our inferType returns Double for non-integer literals
    y = 2;        // int
    z = x + y;    // promote and compute
    print("x: {}\n", x);
    print("y: {}\n", y);
    print("z: {}\n", z);
    ret 0;
}
