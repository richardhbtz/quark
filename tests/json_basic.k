import "lib/json/json.k"

//EXPECT: "x"
int main() {
    var j: str = "{\"a\":1, \"b\":[10,20], \"c\":{\"d\":\"x\"}}";
    var js = Json.new(j);
    var v3 = js.get("c.d");
    print("{}", v3);

    ret 0;
}
