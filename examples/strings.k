// String Operations in Quark

int main() {
    var greeting = "Hello";
    var target = "World";

    // String concatenation
    var message = greeting + ", " + target + "!";
    print("{}", message);

    // String length
    print("{}", format("Length of greeting: {}", greeting.length()));

    // String slicing
    print("{}", format("First 2 chars: {}", greeting.slice(0, 2)));
    print("{}", format("Last 3 chars: {}", greeting.slice(2, 5)));

    // Find substring (returns true/false)
    print("{}", format("Contains 'llo': {}", greeting.find("llo")));
    print("{}", format("Contains 'xyz': {}", greeting.find("xyz")));

    // String formatting
    var name = "Alice";
    var age = 30;
    print("{}", format("{} is {} years old", name, age));

    ret 0;
}
