// Test that error reporting works correctly for imported files
// EXPECT-ERROR: undefined function 'nonexistent_function'

import json

int main() {
    var x = nonexistent_function();
    ret 0;
}
