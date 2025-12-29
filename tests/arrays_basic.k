// EXPECT: 3
// EXPECT: 4
// EXPECT: 8
int main() {
    var arr = [1,2,3];
    print("{}", arr[2]);
    arr[1] = 4;
    print("{}", arr[1]);
    print("{}", arr[0] + arr[1] + arr[2]); // Adjusted to reflect the new sum
    ret 0;
}
