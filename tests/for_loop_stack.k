// Test for loop stack behavior
// This test verifies that for loops don't cause stack overflow
// by declaring variables inside the loop

int main() {
    print("Testing for loop with variables inside...");
    
    // This used to cause stack overflow because allocas were placed
    // inside the loop body instead of the entry block
    for (i in 0..100) {
        var x = i * 2;
        var y = x + 1;
        if (i == 99) {
            print("Last iteration: x={}, y={}", x, y);
        }
    }
    
    print("For loop completed successfully!");
    
    // Test nested for loops
    var sum = 0;
    for (i in 0..10) {
        for (j in 0..10) {
            var product = i * j;
            sum = sum + product;
        }
    }
    print("Nested loop sum: {}", sum);
    
    // Test for loop with many iterations (would overflow stack before fix)
    var count = 0;
    for (k in 0..1000) {
        var temp = k;
        count = count + 1;
    }
    print("Large loop count: {}", count);
    
    ret 0;
}

// EXPECT: Testing for loop with variables inside...
// EXPECT: Last iteration: x=198, y=199
// EXPECT: For loop completed successfully!
// EXPECT: Nested loop sum: 2025
// EXPECT: Large loop count: 1000
