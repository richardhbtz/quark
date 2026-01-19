// Test: Extern functions with named parameters and void*
// EXPECT: 42
// EXPECT: 7
// EXPECT: Callback result: 15

extern "C" {
    // Test named parameters (Note: using abs_i32 builtin instead of C abs because builtin shadows it)
    // int abs(value: int);
    
    // Test void* parameters  
    void* malloc(size: int);
    void free(ptr: void*);
    
    // Test mixed (backwards compatible - type only)
    int toupper(int);
}

int add(a: int, b: int) {
    ret a + b;
}

int main() {
    // Test abs_i32 builtin (integer absolute value)
    print("{}", abs_i32(-42));
    print("{}", abs_i32(-7));
    
    // Test void* function pointer
    void* callback = &add;
    int result = callback(7, 8);
    print("{}", format("Callback result: {}", result));
    
    // Test null pointer
    void* ptr = null;
    
    ret 0;
}
