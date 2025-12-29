// Math Builtins

int main() {
    // Absolute value
    print("{}", format("abs(-5) = {}", abs_i32(-5)));
    print("{}", format("abs(-3.5) = {}", abs_f64(-3.5)));

    // Min and max
    print("{}", format("min(3, 7) = {}", min(3, 7)));
    print("{}", format("max(3, 7) = {}", max(3, 7)));

    // Float min/max
    print("{}", format("min(3.0, 7.5) = {}", min(3.0, 7.5)));
    print("{}", format("max(3.0, 7.5) = {}", max(3.0, 7.5)));

    // Clamp - constrains a value within a range
    print("{}", format("clamp(5, 0, 3) = {}", clamp(5, 0, 3)));    // 3 (clamped to max)
    print("{}", format("clamp(-2, 0, 3) = {}", clamp(-2, 0, 3)));  // 0 (clamped to min)
    print("{}", format("clamp(2, 0, 3) = {}", clamp(2, 0, 3)));    // 2 (within range)

    // Type casting
    int x = 7;
    var f = (float)x;
    print("{}", format("int {} as float: {}", x, f));

    var pi = 3.14159;
    var truncated = (int)pi;
    print("{}", format("float {} as int: {}", pi, truncated));

    ret 0;
}
