#include "lib/json/json.k"

// EXPECT: Array length: 3
// EXPECT: Item 0: apple
// EXPECT: Item 1: banana
// EXPECT: Item 2: cherry
// EXPECT: Nested array length: 3
// EXPECT: Nested value 0: 10
int main() {
    var json = Json->new();
    
    // Test JSON with an array
    var test_json = "{\"items\": [\"apple\", \"banana\", \"cherry\"], \"count\": 3}";
    
    // Get array length
    var len = json.get_array_length(test_json, "items");
    print("Array length: {}", len);
    
    // Get individual items
    var item0 = json.get_array_string(test_json, "items", 0);
    var item1 = json.get_array_string(test_json, "items", 1);
    var item2 = json.get_array_string(test_json, "items", 2);
    
    print("Item 0: {}", item0);
    print("Item 1: {}", item1);
    print("Item 2: {}", item2);
    
    // Test nested array
    var nested_json = "{\"data\": {\"values\": [10, 20, 30]}}";
    var nested_len = json.get_array_length(nested_json, "data.values");
    print("Nested array length: {}", nested_len);
    
    var val0 = json.get_array_item(nested_json, "data.values", 0);
    print("Nested value 0: {}", val0);
    
    ret 0;
}

