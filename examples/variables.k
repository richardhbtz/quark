// Variables and Type Inference

int main() {
    // Explicit type declaration
    int age = 25;
    float pi = 3.14159;
    str name = "Quark";
    bool active = true;

    // Type inference with 'var'
    var count = 100;        // inferred as int
    var ratio = 0.5;        // inferred as float
    var message = "Hello";  // inferred as str

    print("{}", format("Name: {}", name));
    print("{}", format("Age: {}", age));
    print("{}", format("Pi: {}", pi));
    print("{}", format("Active: {}", active));
    print("{}", format("Count: {}", count));

    ret 0;
}
