// Test bare module import syntax
import json

int main() {
    str json_string = "{\"msg\": \"Hello from bare import!\"}";
    var js = Json.new(json_string);
    var msg = js.get("msg");
    print("{}", format("Message: {}", msg));
    ret 0;
}

// EXPECT: Message: "Hello from bare import!"
