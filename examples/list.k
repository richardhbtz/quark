// Comprehensive list example demonstrating the built-in list type
// Note: list is an untyped container that stores all values as pointer-sized integers

int main() {
    print("=== Creating a list ===");
    list numbers;
    
    print("\n=== Adding elements ===");
    numbers.push(10);
    numbers.push(20);
    numbers.push(30);
    numbers.push(40);
    
    print("List size: {}", numbers.len());
    
    print("\n=== Accessing elements by index ===");
    print("First element: {}", numbers[0]);
    print("Second element: {}", numbers[1]);
    print("Third element: {}", numbers[2]);
    print("Fourth element: {}", numbers[3]);
    
    print("\n=== Modifying elements ===");
    numbers[1] = 25;
    print("Modified second element: {}", numbers[1]);
    
    print("\n=== Removing elements ===");
    numbers.remove(2);
    print("List size after removal: {}", numbers.len());
    print("New third element: {}", numbers[2]);
    
    print("\n=== List with initializer ===");
    list scores = [95, 87, 92, 78, 88];
    print("Scores list size: {}", scores.len());
    print("First score: {}", scores[0]);
    print("Last score: {}", scores[4]);
    
    print("\n=== Done! ===");
    
    ret 0;
}
