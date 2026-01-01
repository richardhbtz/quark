// Structs with Methods

struct Point {
    data {
        x: int,
        y: int
    }

    // Static constructor
    Point new(x: int, y: int) {
        ret Point { x: x, y: y };
    }

    // Instance methods
    int sum() {
        ret x + y;
    }

    str to_string() {
        ret format("Point({}, {})", x, y);
    }
}

struct Rectangle {
    data {
        width: int,
        height: int
    }

    Rectangle new(w: int, h: int) {
        ret Rectangle { width: w, height: h };
    }

    int area() {
        ret width * height;
    }

    int perimeter() {
        ret 2 * (width + height);
    }
}

int main() {
    // Create struct using static constructor
    var p = Point.new(10, 20);
    print("{}", p.to_string());
    print("{}", format("Sum: {}", p.sum()));

    // Create struct using literal syntax
    var p2 = Point { x: 5, y: 15 };
    print("{}", format("P2 sum: {}", p2.sum()));

    // Rectangle example
    var rect = Rectangle.new(10, 5);
    print("{}", format("Area: {}", rect.area()));
    print("{}", format("Perimeter: {}", rect.perimeter()));

    ret 0;
}
