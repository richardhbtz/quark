// Pointers in Quark

int main() {
    // Basic pointer usage
    int value = 42;
    int* ptr = &value;  // Get address of value

    print("{}", format("Value: {}", value));
    print("{}", format("Via pointer: {}", *ptr));

    // Modify value through pointer
    *ptr = 100;
    print("{}", format("After modification: {}", value));

    // Pointer to pointer
    int** pptr = &ptr;
    **pptr = 200;
    print("{}", format("Via double pointer: {}", value));

    // Swap values using pointers
    int a = 10;
    int b = 20;
    print("{}", format("Before swap: a={}, b={}", a, b));

    int* pa = &a;
    int* pb = &b;
    int temp = *pa;
    *pa = *pb;
    *pb = temp;

    print("{}", format("After swap: a={}, b={}", a, b));

    ret 0;
}
