// EXPECT: name=alice age=30 ok=true score=12.340000
int main() {
    var name: str = "alice";
    var age: int = 30;
    var ok: bool = true;
    var score = 12.34;
    print("{}", format("name={} age={} ok={} score={}", name, age, ok, score));
    ret 0;
}
