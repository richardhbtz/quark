//EXPECT: 15
//EXPECT: 10
//EXPECT: 50
//EXPECT: 10
//EXPECT: 1
int main() {
    var x = 10;
    x += 5;
    print("{}", x);
    
    x -= 5;
    print("{}", x);
    
    x *= 5;
    print("{}", x);
    
    x /= 5;
    print("{}", x);
    
    x %= 3;
    print("{}", x);
    
    ret 0;
}
