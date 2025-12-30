// Simple sleep builtin test
// EXPECT: before
// EXPECT: after

int main() {
    print("before");
    sleep(10);
    print("after");
    ret 0;
}
