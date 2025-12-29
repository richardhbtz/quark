// EXPECT: John
// EXPECT: Engineer
// EXPECT: Working: Engineer
struct Human {
    data {
        name: str,
        age: int
    }

    extend str to_string() {
        ret name;
    }

    Human new(name: str, age: int) {
        ret Human { name: name, age: age };
    }

    str greet() {
        ret name;
    }
}

struct Professional : Human {
    data {
        title: str,
        salary: int
    }

    impl str to_string() {
        ret this.title;
    }

    Professional new(name: str, age: int, title: str, salary: int) {
        ret Professional { name: name, age: age, title: title, salary: salary };
    }

    str work() {
        ret this.title;
    }
}

int main() {
    var human = Human->new("John", 25);
    print("{}", human.greet());
    var prof = Professional->new("Alice", 30, "Engineer", 100000);
    print("{}", prof.to_string());
    print("{}", format("Working: {}", prof.work()));
    ret 0;
}
