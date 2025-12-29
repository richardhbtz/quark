#include "lib/json/json.k"

//EXPECT: "x"
int main() {
    var j: str = "{\"a\":1, \"b\":[10,20], \"c\":{\"d\":\"x\"}}";
    var js = Json->new();
    var v3 = js.get(j, "c.d");
    print("{}", v3);

    ret 0;
}
