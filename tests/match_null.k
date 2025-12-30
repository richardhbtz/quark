// Test match statement and null literal

// EXPECT: one
// EXPECT: other
// EXPECT: hello
// EXPECT: unknown
// EXPECT: pointer is null

int main() {
    // Test match with integers
    var x = 1;
    match x {
        1 => { print("one"); },
        2 => { print("two"); },
        _ => { print("other"); }
    }
    
    // Test match with different value
    var y = 5;
    match y {
        1 => { print("one"); },
        2 => { print("two"); },
        _ => { print("other"); }
    }
    
    // Test match with strings
    var s = "hello";
    match s {
        "hi" => { print("hi"); },
        "hello" => { print("hello"); },
        _ => { print("unknown"); }
    }
    
    // Test match with fallthrough to wildcard
    var z = 99;
    match z {
        1 => { print("one"); },
        _ => { print("unknown"); }
    }
    
    // Test null literal
    var ptr = null;
    if (ptr == null) {
        print("pointer is null");
    }
    
    ret 0;
}
