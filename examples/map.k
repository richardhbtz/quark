// Comprehensive map (dictionary/hashmap) example

int main() {
    println("=== Creating a map ===");
    var users = map_new();
    
    println("\n=== Adding entries ===");
    map_set(users, "alice", "Engineer");
    map_set(users, "bob", "Designer");
    map_set(users, "charlie", "Manager");
    
    println("Map size: {}", map_len(users));
    
    println("\n=== Retrieving values ===");
    println("Alice's role: {}", map_get(users, "alice"));
    println("Bob's role: {}", map_get(users, "bob"));
    
    println("\n=== Checking existence ===");
    if (map_has(users, "alice")) {
        println("Alice exists in map");
    }
    
    if (!map_has(users, "david")) {
        println("David does not exist in map");
    }
    
    println("\n=== Updating values ===");
    map_set(users, "alice", "Senior Engineer");
    println("Alice's new role: {}", map_get(users, "alice"));
    
    println("\n=== Removing entries ===");
    if (map_remove(users, "bob")) {
        println("Removed bob from map");
    }
    println("Map size after removal: {}", map_len(users));
    
    println("\n=== Using maps for configuration ===");
    var config = map_new();
    map_set(config, "host", "localhost");
    map_set(config, "port", "8080");
    map_set(config, "protocol", "http");
    
    var url = map_get(config, "protocol") + "://" + map_get(config, "host") + ":" + map_get(config, "port");
    println("URL: {}", url);
    
    println("\n=== Cleanup ===");
    map_free(users);
    map_free(config);
    println("Maps freed");
    
    ret 0;
}
