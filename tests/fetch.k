#include "lib/http/http.k"
#include "lib/json/json.k"

//EXPECT: delectus aut autem
int main()
{
    var httpClient = Http.new();
    var jsonParser = Json.new();
    str content = httpClient.get("https://jsonplaceholder.typicode.com/todos/1");

    str title = jsonParser.get_string(content, "title");

    print(title);
    ret 0;
}