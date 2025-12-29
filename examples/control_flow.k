// Control Flow - if/elif/else, while, for loops

int main() {
    // If-elif-else
    var score = 85;

    if (score >= 90) {
        print("{}", "Grade: A");
    } elif (score >= 80) {
        print("{}", "Grade: B");
    } elif (score >= 70) {
        print("{}", "Grade: C");
    } else {
        print("{}", "Grade: F");
    }

    // For loop with range (exclusive end)
    print("{}", "Counting 0 to 4:");
    for (i in 0..5) {
        print("{}", i);
    }

    // While loop
    print("{}", "While loop countdown:");
    var countdown = 5;
    while (countdown > 0) {
        print("{}", countdown);
        countdown = countdown - 1;
    }
    print("{}", "Liftoff!");

    // For loop with break and continue
    print("{}", "Numbers (skip 3, stop at 7):");
    for (i in 0..10) {
        if (i == 3) {
            continue;
        }
        if (i == 7) {
            break;
        }
        print("{}", i);
    }

    ret 0;
}
