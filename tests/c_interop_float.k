// EXPECT: 3.140000
// EXPECT: 3.000000
extern "C" {
    double fabs(double);
    double sqrt(double);
}

int main() {
    var f: float = -3.14;
    var d: double = 9.0;
    print(fabs(f));               // 3.14 (float -> double)
    print(sqrt(d));               // 3
    ret 0;
}
