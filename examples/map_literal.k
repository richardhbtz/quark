// Map literal syntax example

// Create a map using literal syntax
var user = ["name": "Alice", "role": "admin"];

print("User: ");
print(user["name"]);
print(" (");
print(user["role"]);
print(")\n");

// Create a map using API
var colors = map_new();
colors.set("red", "#FF0000");
colors.set("blue", "#0000FF");

print("Red: ");
print(colors["red"]);
print("\n");

print("Map has ");
print(colors.len());
print(" colors\n");

// Cleanup
colors.free();
user.free();
