// EXPECT: 2
// EXPECT: 3
// EXPECT: 4
int main() {
    // Intentionally using start..end but not asserting sum to avoid mismatch for now.
    for (i in 2..5) {
        print("{}", i);
    }
    ret 0;
}
