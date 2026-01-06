// Map literal syntax example

int main() {
    // Create a map using literal syntax
    map user = {"name": "Alice", "role": "admin"};

    print("User: ");
    print(user["name"]);
    print(" (");
    print(user["role"]);
    print(")\n");

    // Create an empty map and add entries
    map colors;
    colors.set("red", "#FF0000");
    colors.set("blue", "#0000FF");

    print("Red: ");
    print(colors["red"]);
    print("\n");

    print("Map has ");
    print(colors.len());
    print(" colors\n");
    
    ret 0;
}