// EXPECT: 5
// EXPECT: hello world
// EXPECT: 10
// EXPECT: 16

str addWorld(this str s) {
    ret s + " world";
}

int doubled(this int n) {
    ret n * 2;
}

int square(this int n) {
    ret n * n;
}

int main() {
    str greeting = "hello";
    
    print(greeting.length());
    print(greeting.addWorld());
    
    int x = 5;
    print(x.doubled());
    
    int y = 4;
    print(y.square());
    
    ret 0;
}
