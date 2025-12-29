// Functions in Quark

// Simple function with return type
int add(a: int, b: int) {
    ret a + b;
}

// Function with multiple parameters
int multiply(a: int, b: int, c: int) {
    ret a * b * c;
}

// Recursive function
int factorial(n: int) {
    if (n <= 1) {
        ret 1;
    }
    ret n * factorial(n - 1);
}

// Function returning a string
str greet(name: str) {
    ret format("Hello, {}!", name);
}

int main() {
    print("{}", format("5 + 3 = {}", add(5, 3)));
    print("{}", format("2 * 3 * 4 = {}", multiply(2, 3, 4)));
    print("{}", format("5! = {}", factorial(5)));
    print("{}", greet("World"));

    ret 0;
}
