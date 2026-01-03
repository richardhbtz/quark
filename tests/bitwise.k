// Test bitwise operators
// EXPECT: 12 & 10 = 8
// EXPECT: 12 | 10 = 14
// EXPECT: 12 ^ 10 = 6
// EXPECT: ~0 = -1
// EXPECT: 1 << 4 = 16
// EXPECT: 16 >> 2 = 4
// EXPECT: combined = 14
// EXPECT: masked = 13

int main() {
    // Test bitwise AND (&)
    int a = 12;  // 1100 in binary
    int b = 10;  // 1010 in binary
    int result = a & b;  // Should be 8 (1000)
    print("12 & 10 = {}", result);
    
    // Test bitwise OR (|)
    result = a | b;  // Should be 14 (1110)
    print("12 | 10 = {}", result);
    
    // Test bitwise XOR (^)
    result = a ^ b;  // Should be 6 (0110)
    print("12 ^ 10 = {}", result);
    
    // Test bitwise NOT (~)
    result = ~0;  // Should be -1 (all bits set)
    print("~0 = {}", result);
    
    // Test left shift (<<)
    result = 1 << 4;  // Should be 16
    print("1 << 4 = {}", result);
    
    // Test right shift (>>)
    result = 16 >> 2;  // Should be 4
    print("16 >> 2 = {}", result);
    
    // Test combined operations
    result = (a & b) | (a ^ b);  // Should be (8 | 6) = 14
    print("combined = {}", result);
    
    // Test bit manipulation
    int mask = 1 << 3;  // Set bit 3 (value = 8)
    int value = 5;
    result = value | mask;  // Should be 13 (5 | 8)
    print("masked = {}", result);
    
    ret 0;
}
