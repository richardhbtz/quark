// C Interoperability

// Declare external C functions
extern "C" {
    int abs(int);
    int toupper(int);
    int tolower(int);
    int putchar(int);
}

int main() {
    // Using C's abs function
    print("{}", format("abs(-42) = {}", abs(-42)));
    print("{}", format("abs(15) = {}", abs(15)));

    // Character manipulation
    var lowercase_a = 97;  // ASCII 'a'
    var uppercase_a = toupper(lowercase_a);

    print("{}", "Converting 'a' to uppercase:");
    putchar(uppercase_a);
    putchar(10);  // newline

    var uppercase_z = 90;  // ASCII 'Z'
    var lowercase_z = tolower(uppercase_z);

    print("{}", "Converting 'Z' to lowercase:");
    putchar(lowercase_z);
    putchar(10);

    ret 0;
}
