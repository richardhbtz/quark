// JSON Parsing using the built-in JSON library

#include "lib/json/json.k"

int main() {
    // Create JSON handler
    var js = Json.new();

    // Parse JSON and extract values
    var json_str: str = "{\"name\":\"Alice\",\"age\":30,\"city\":\"Paris\"}";

    var name = js.get(json_str, "name");
    var age = js.get(json_str, "age");
    var city = js.get(json_str, "city");

    print("{}", format("Name: {}", name));
    print("{}", format("Age: {}", age));
    print("{}", format("City: {}", city));

    // Nested JSON access
    var nested: str = "{\"user\":{\"profile\":{\"username\":\"quark_dev\"}}}";
    var username = js.get(nested, "user.profile.username");
    print("{}", format("Username: {}", username));

    // Array access in JSON
    var with_array: str = "{\"items\":[10,20,30],\"status\":\"ok\"}";
    var status = js.get(with_array, "status");
    print("{}", format("Status: {}", status));

    ret 0;
}
