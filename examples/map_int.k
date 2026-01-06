// Test map with integer values

int main() {
    print("Creating map with integers\n");
    map ages = {
        "alice": 30,
        "bob": 25
    };
    
    print("Getting alice's age\n");
    var aliceAge = ages.get("alice");
    print("Alice age pointer: {}\n", aliceAge);
    
    ret 0;
}
