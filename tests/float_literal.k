// Test float literal syntax (e.g., 1.0f)
// EXPECT: c is zero
// EXPECT: a = 1.000000
// EXPECT: b = 2.500000
// EXPECT: sum = 3.500000
// EXPECT: product = 2.000000
// EXPECT: result = 6.283180
// EXPECT: neg = -1.500000

int main() {
    // Test basic float literals
    float a = 1.0f;
    float b = 2.5f;
    float c = 0.0f;
    
    // Test arithmetic with float literals
    float sum = a + b;
    float product = a * 2.0f;
    
    // Test comparison
    if (c == 0.0f) {
        print("c is zero");
    }
    
    // Test float in expressions
    float result = 3.14159f * 2.0f;
    
    // Test negative float
    float neg = -1.5f;
    
    print("a = {}", a);
    print("b = {}", b);
    print("sum = {}", sum);
    print("product = {}", product);
    print("result = {}", result);
    print("neg = {}", neg);
    
    ret 0;
}
