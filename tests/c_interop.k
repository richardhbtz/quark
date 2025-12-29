// EXPECT: 42
// EXPECT: 7
// EXPECT: A
// EXPECT: z
extern "C" {
    int abs(int);
    int toupper(int);
    int tolower(int);
    int putchar(int);
}

int main() {
    print("{}", abs(-42));
    print("{}", abs(-7));
    var a: int = 97;   // 'a'
    var A: int = toupper(a);
    putchar(A);
    putchar(10);       // newline
    var Z: int = 90;   // 'Z'
    var z: int = tolower(Z);
    putchar(z);
    putchar(10);
    ret 0;
}
