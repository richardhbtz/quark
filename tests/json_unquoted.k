#include "lib/json/json.k"

// EXPECT: x
int main() {
    var j: str = "{\"a\":1, \"b\":[10,20], \"c\":{\"d\":\"x\"}}";
    var js = Json.new();
    var v = js.get_string(j, "c.d");
    print(v);
    ret 0;
}
