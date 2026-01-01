// JSON Parsing using the built-in JSON library

#include "lib/json/json.k"

int main() {
    // Parse JSON and extract values
    var json_str: str = "{\"name\":\"Alice\",\"age\":30,\"city\":\"Paris\"}";
    var js = Json.new(json_str);

    var name = js.get("name");
    var age = js.get("age");
    var city = js.get("city");

    print("{}", format("Name: {}", name));
    print("{}", format("Age: {}", age));
    print("{}", format("City: {}", city));

    // Nested JSON access
    var nested: str = "{\"user\":{\"profile\":{\"username\":\"quark_dev\"}}}";
    var js2 = Json.new(nested);
    var username = js2.get("user.profile.username");
    print("{}", format("Username: {}", username));

    // Array access in JSON
    var with_array: str = "{\"items\":[10,20,30],\"status\":\"ok\"}";
    var js3 = Json.new(with_array);
    var status = js3.get("status");
    print("{}", format("Status: {}", status));

    ret 0;
}
