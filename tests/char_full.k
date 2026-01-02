// EXPECT: A = 65
// EXPECT: newline = 10
// EXPECT: tab = 9
// EXPECT: single quote = 39
// EXPECT: backslash = 92
// EXPECT: null = 0

int main() {
    // Test basic character
    var a: char = 'A';
    print("A = {}", (int)a);
    
    // Test escape sequences
    var newline: char = '\n';
    print("newline = {}", (int)newline);
    
    var tab: char = '\t';
    print("tab = {}", (int)tab);
    
    var quote: char = '\'';
    print("single quote = {}", (int)quote);
    
    var backslash: char = '\\';
    print("backslash = {}", (int)backslash);
    
    var null_char: char = '\0';
    print("null = {}", (int)null_char);
    
    ret 0;
}
