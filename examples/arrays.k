// Arrays in Quark

int main() {
    // Array declaration
    var numbers = [10, 20, 30, 40, 50];

    // Access elements by index
    print("{}", format("First element: {}", numbers[0]));
    print("{}", format("Third element: {}", numbers[2]));

    // Modify elements
    numbers[1] = 25;
    print("{}", format("Modified second element: {}", numbers[1]));

    // Array length
    print("{}", format("Array length: {}", numbers.length()));

    // Array slicing: slice(start, end, step)
    var slice = numbers.slice(1, 4, 1);
    print("{}", format("Slice length: {}", slice.length()));

    // Sum array elements
    var sum = 0;
    for (i in 0..5) {
        sum = sum + numbers[i];
    }
    print("{}", format("Sum: {}", sum));

    ret 0;
}
