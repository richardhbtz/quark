#include "lib/toml/toml.k"
#include "lib/json/json.k"

//EXPECT: name=Quark|version=2|color=blue|count=3|json={"features":{"color":"blue","count":3},"name":"Quark","version":2}
int main() {
    var doc = Toml.new();
    doc.load_string("name = \"Quark\"\nversion = 1\n[features]\ncolor = \"red\"\ncount = 2\n");

    doc.set_int("version", 2);
    doc.set_string("features.color", "blue");
    doc.set_int("features.count", doc.get_int("features.count") + 1);

    var name = doc.get_string("name");
    var version = doc.get_int("version");
    var color = doc.get_string("features.color");
    var count = doc.get_int("features.count");
    var json = doc.to_json();

    var js = Json.new(json);
    print("name={}|version={}|color={}|count={}|json={}", name, version, color, count, js.minify());

    doc.free_string(name);
    doc.free_string(color);
    doc.free_string(json);
    doc.destroy();

    ret 0;
}
