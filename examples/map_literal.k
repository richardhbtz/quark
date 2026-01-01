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
map_set(colors, "red", "#FF0000");
map_set(colors, "blue", "#0000FF");

print("Red: ");
print(colors["red"]);
print("\n");

print("Map has ");
print(map_len(colors));
print(" colors\n");

// Cleanup
map_free(colors);
map_free(user);
