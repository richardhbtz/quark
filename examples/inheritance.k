// Struct Inheritance

struct Animal {
    data {
        name: str,
        age: int
    }

    // Base method (can be extended by children)
    extend str speak() {
        ret "...";
    }

    str get_info() {
        ret format("{} is {} years old", name, age);
    }
}

struct Dog : Animal {
    data {
        breed: str
    }

    Dog new(name: str, age: int, breed: str) {
        ret Dog { name: name, age: age, breed: breed };
    }

    // Override parent method
    extend str speak() {
        ret "Woof!";
    }

    str get_breed() {
        ret breed;
    }
}

struct Cat : Animal {
    data {
        indoor: bool
    }

    Cat new(name: str, age: int, indoor: bool) {
        ret Cat { name: name, age: age, indoor: indoor };
    }

    extend str speak() {
        ret "Meow!";
    }
}

int main() {
    var dog = Dog.new("Buddy", 3, "Golden Retriever");
    var cat = Cat.new("Whiskers", 5, true);

    print("{}", dog.get_info());
    print("{}", format("Breed: {}", dog.get_breed()));
    print("{}", format("Dog says: {}", dog.speak()));

    print("{}", cat.get_info());
    print("{}", format("Cat says: {}", cat.speak()));

    ret 0;
}
