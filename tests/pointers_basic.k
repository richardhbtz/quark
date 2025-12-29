// EXPECT: 10
// EXPECT: 20
// EXPECT: 20
// EXPECT: 30
// EXPECT: 40
int main() {
    // Basic pointer declaration and usage
    int x = 10;         // typed declaration
    int* px = &x;       // pointer to x
    print("{}", x);    // 10
    *px = 20;           // mutate x through pointer
    print("{}", x);    // 20
    print("{}", *px);  // 20

    // Pointer to pointer
    int** ppx = &px;    // pointer to pointer
    *ppx = &x;          // redundant, but exercises double pointer assignment
    *px = *px + 10;     // x = 30
    print("{}", x);    // 30

    // Direct memory address round-trip using numeric cast (unsafe, for demonstration only)
    double raw_addr = (double)px;  // capture pointer value as a numeric address
    int* direct_px = (int*)raw_addr; // recreate pointer from the raw address
    *direct_px = 40;     // mutate via reconstructed pointer
    print("{}", x);     // 40
    ret 0;
}
