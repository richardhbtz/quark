// Test: Function Pointers
// EXPECT: Direct add: 8
// EXPECT: Direct sub: 6
// EXPECT: Through pointer (add): 15
// EXPECT: Re-assigned pointer (sub): 7
// EXPECT: Doubled: 20
// EXPECT: Void function called

// Basic functions to test with
int add(a: int, b: int) {
    ret a + b;
}

int sub(a: int, b: int) {
    ret a - b;
}

int double_it(x: int) {
    ret x * 2;
}

void say_hello() {
    print("{}", format("Void function called"));
}

// Main program
int main() {
    // Test direct calls first
    int r1 = add(3, 5);
    print("{}", format("Direct add: {}", r1));
    
    int r2 = sub(10, 4);
    print("{}", format("Direct sub: {}", r2));
    
    // Test function pointer with void*
    void* callback = &add;
    int result = callback(7, 8);
    print("{}", format("Through pointer (add): {}", result));
    
    // Test reassigning function pointer
    callback = &sub;
    int result2 = callback(10, 3);
    print("{}", format("Re-assigned pointer (sub): {}", result2));
    
    // Test with var type inference
    var doubler = &double_it;
    int doubled = doubler(10);
    print("{}", format("Doubled: {}", doubled));
    
    // Test void function pointer
    void* voidFn = &say_hello;
    voidFn();
    
    ret 0;
}
