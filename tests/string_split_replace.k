// Test string split and replace builtins

// EXPECT: Testing str_replace:
// EXPECT: Say goodbye world
// EXPECT: Say goodbye goodbye goodbye world
// EXPECT: Testing str_split:
// EXPECT: Part 0: hello
// EXPECT: Part 1: world
// EXPECT: Part 2: test
// EXPECT: Split with colon:
// EXPECT: apple
// EXPECT: banana
// EXPECT: cherry

int main() {
    // Test str_replace (method syntax)
    print("Testing str_replace:");
    
    str original = "Say hello world";
    str replaced = original.replace("hello", "goodbye");
    print(replaced);
    
    str multi = "Say hello hello hello world";
    str replaced2 = multi.replace("hello", "goodbye");
    print(replaced2);
    
    // Test str_split (method syntax)
    print("Testing str_split:");
    
    str toSplit = "hello,world,test";
    str[] parts = toSplit.split(",");
    
    var i = 0;
    var len = parts.length();
    while (i < len) {
        print("Part {}: {}", i, parts[i]);
        i = i + 1;
    }
    
    // Test with different delimiter
    print("Split with colon:");
    str fruits = "apple:banana:cherry";
    str[] fruitParts = fruits.split(":");
    
    i = 0;
    len = fruitParts.length();
    while (i < len) {
        print(fruitParts[i]);
        i = i + 1;
    }
    
    ret 0;
}
