// Test file for various integer types
// Note: Only i32/int are fully tested; other types need print format improvements

int main() {
    // Signed 32-bit integer types (these work with print formatting)
    i32 c = 100;
    int i = 42;
    
    // Print values
    print("i32 value: {}", c);
    print("int value: {}", i);
    
    // Test arithmetic operations
    i32 sum = c + i;
    print("sum: {}", sum);
    
    ret 0;
}

// EXPECT: i32 value: 100
// EXPECT: int value: 42
// EXPECT: sum: 142
