// Test standardized built-in functions and their aliases

int main() {
    // Test type conversion functions
    print("=== Type Conversion Tests ===");
    
    // to_string and toString alias
    var s1 = to_string(42);
    print("to_string(42) = {}", s1);
    // EXPECT: to_string(42) = 42
    
    var s2 = toString(3.14);
    print("toString(3.14) = {}", s2);
    // EXPECT: toString(3.14) = 3.14
    
    var s3 = to_string(true);
    print("to_string(true) = {}", s3);
    // EXPECT: to_string(true) = true
    
    // to_int, parse_int, and parseInt aliases
    var n1 = to_int("123");
    print("to_int(\"123\") = {}", n1);
    // EXPECT: to_int("123") = 123
    
    var n2 = parse_int("456");
    print("parse_int(\"456\") = {}", n2);
    // EXPECT: parse_int("456") = 456
    
    var n3 = parseInt("789");
    print("parseInt(\"789\") = {}", n3);
    // EXPECT: parseInt("789") = 789
    
    var n4 = to_int(3.14);
    print("to_int(3.14) = {}", n4);
    // EXPECT: to_int(3.14) = 3
    
    var n5 = to_int(true);
    print("to_int(true) = {}", n5);
    // EXPECT: to_int(true) = 1
    
    // Test string functions
    print("\n=== String Functions ===");
    
    var str1 = "Hello";
    var str2 = " World";
    var concat = str_concat(str1, str2);
    print("str_concat(\"{}\", \"{}\") = {}", str1, str2, concat);
    // EXPECT: str_concat("Hello", " World") = Hello World
    
    var len = str_len("Hello");
    print("str_len(\"Hello\") = {}", len);
    // EXPECT: str_len("Hello") = 5
    
    var len2 = str_length("World");
    print("str_length(\"World\") = {}", len2);
    // EXPECT: str_length("World") = 5
    
    var slice = str_slice("Hello World", 0, 5);
    print("str_slice(\"Hello World\", 0, 5) = {}", slice);
    // EXPECT: str_slice("Hello World", 0, 5) = Hello
    
    var starts = str_starts_with("Hello", "He");
    print("str_starts_with(\"Hello\", \"He\") = {}", starts);
    // EXPECT: str_starts_with("Hello", "He") = true
    
    var ends = str_ends_with("World", "ld");
    print("str_ends_with(\"World\", \"ld\") = {}", ends);
    // EXPECT: str_ends_with("World", "ld") = true
    
    // Test math functions
    print("\n=== Math Functions ===");
    
    double val16 = 16.0;
    double sq = sqrt(val16);
    print("sqrt(16.0) = {}", sq);
    // EXPECT: sqrt(16.0) = 4
    
    double base2 = 2.0;
    double exp3 = 3.0;
    double p = pow(base2, exp3);
    print("pow(2.0, 3.0) = {}", p);
    // EXPECT: pow(2.0, 3.0) = 8
    
    int a1 = abs_i32(-42);
    print("abs_i32(-42) = {}", a1);
    // EXPECT: abs_i32(-42) = 42
    
    int minVal = min_i32(10, 20);
    print("min_i32(10, 20) = {}", minVal);
    // EXPECT: min_i32(10, 20) = 10
    
    int maxVal = max_i32(10, 20);
    print("max_i32(10, 20) = {}", maxVal);
    // EXPECT: max_i32(10, 20) = 20
    
    int clamped = clamp_i32(15, 0, 10);
    print("clamp_i32(15, 0, 10) = {}", clamped);
    // EXPECT: clamp_i32(15, 0, 10) = 10
    
    int clamped2 = clamp_i32(5, 0, 10);
    print("clamp_i32(5, 0, 10) = {}", clamped2);
    // EXPECT: clamp_i32(5, 0, 10) = 5
    
    print("\n=== I/O Functions ===");
    
    var formatted = format("Hello {}, you are {} years old", "Alice", 30);
    print(formatted);
    // EXPECT: Hello Alice, you are 30 years old
    
    print("All builtin tests passed!");
    // EXPECT: All builtin tests passed!
    
    ret 0;
}
