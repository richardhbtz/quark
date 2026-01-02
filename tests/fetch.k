import http
import json

//EXPECT: delectus aut autem
int main()
{
    var httpClient = Http.new();
    str content = httpClient.get("https://jsonplaceholder.typicode.com/todos/1");
    var jsonParser = Json.new(content);

    str title = jsonParser.get_string("title");

    print(title);
    ret 0;
}