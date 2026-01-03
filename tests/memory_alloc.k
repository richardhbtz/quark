// Test: Manual memory allocation builtins
// EXPECT: Allocating memory...
// EXPECT: Writing to allocated memory...
// EXPECT: Values: 1, 2, 3, 4, 5
// EXPECT: Reallocating memory...
// EXPECT: Extended values: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
// EXPECT: Freeing memory...
// EXPECT: Memory test passed!

int main() {
    print("Allocating memory...");
    
    // Allocate space for 5 integers (5 * 4 bytes = 20 bytes)
    void* raw = alloc(20);
    
    // Cast to int pointer (note: 'data' is reserved, use 'ptr')
    int* ptr = (int*)raw;
    
    if (ptr == (int*)0) {
        print("Allocation failed!");
        ret 1;
    }
    
    print("Writing to allocated memory...");
    
    // Write values
    ptr[0] = 1;
    ptr[1] = 2;
    ptr[2] = 3;
    ptr[3] = 4;
    ptr[4] = 5;
    
    // Read values back
    print("Values: {}, {}, {}, {}, {}", ptr[0], ptr[1], ptr[2], ptr[3], ptr[4]);
    
    print("Reallocating memory...");
    
    // Reallocate to hold 10 integers (40 bytes)
    raw = realloc(raw, 40);
    ptr = (int*)raw;
    
    if (ptr == (int*)0) {
        print("Reallocation failed!");
        ret 1;
    }
    
    // Write more values
    ptr[5] = 6;
    ptr[6] = 7;
    ptr[7] = 8;
    ptr[8] = 9;
    ptr[9] = 10;
    
    print("Extended values: {}, {}, {}, {}, {}, {}, {}, {}, {}, {}", 
          ptr[0], ptr[1], ptr[2], ptr[3], ptr[4],
          ptr[5], ptr[6], ptr[7], ptr[8], ptr[9]);
    
    print("Freeing memory...");
    free(raw);
    
    print("Memory test passed!");
    ret 0;
}
