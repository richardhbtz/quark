import "lib/json/json.k"

// EXPECT: Array length: 3
// EXPECT: Item 0: apple
// EXPECT: Item 1: banana
// EXPECT: Item 2: cherry
// EXPECT: Nested array length: 3
// EXPECT: Nested value 0: 10
int main() {
    var test_json = "{\"items\": [\"apple\", \"banana\", \"cherry\"], \"count\": 3}";
    var json = Json.new(test_json);
    
    // Test JSON with an array
    
    // Get array length
    var len = json.get_array_length("items");
    print("Array length: {}", len);
    
    // Get individual items
    var item0 = json.get_array_string("items", 0);
    var item1 = json.get_array_string("items", 1);
    var item2 = json.get_array_string("items", 2);
    
    print("Item 0: {}", item0);
    print("Item 1: {}", item1);
    print("Item 2: {}", item2);
    
    // Test nested array
    var nested_json = "{\"data\": {\"values\": [10, 20, 30]}}";
    var json2 = Json.new(nested_json);
    var nested_len = json2.get_array_length("data.values");
    print("Nested array length: {}", nested_len);
    
    var val0 = json2.get_array_item("data.values", 0);
    print("Nested value 0: {}", val0);
    
    ret 0;
}

