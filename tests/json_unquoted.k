import json

// EXPECT: x
int main() {
    var j: str = "{\"a\":1, \"b\":[10,20], \"c\":{\"d\":\"x\"}}";
    var js = Json.new(j);
    var v = js.get_string("c.d");
    print(v);
    ret 0;
}
