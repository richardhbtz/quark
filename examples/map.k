// Comprehensive map (dictionary/hashmap) example

int main() {
    print("=== Creating a map ===");
    var users = map_new();
    
    print("\n=== Adding entries ===");
    users.set("alice", "Engineer");
    users.set("bob", "Designer");
    users.set("charlie", "Manager");
    
    print("Map size: {}", users.len());
    
    print("\n=== Retrieving values ===");
    print("Alice's role: {}", users.get("alice"));
    print("Bob's role: {}", users.get("bob"));
    
    print("\n=== Checking existence ===");
    if (users.has("alice")) {
        print("Alice exists in map");
    }
    
    if (!users.has("david")) {
        print("David does not exist in map");
    }
    
    print("\n=== Updating values ===");
    users.set("alice", "Senior Engineer");
    print("Alice's new role: {}", users.get("alice"));
    
    print("\n=== Removing entries ===");
    if (users.remove("bob")) {
        print("Removed bob from map");
    }
    print("Map size after removal: {}", users.len());
    
    print("\n=== Using maps for configuration ===");
    var config = map_new();
    config.set("host", "localhost");
    config.set("port", "8080");
    config.set("protocol", "http");
    
    var url = config.get("protocol") + "://" + config.get("host") + ":" + config.get("port");
    print("URL: {}", url);
    
    print("\n=== Cleanup ===");
    users.free();
    config.free();
    print("Maps freed");
    
    ret 0;
}
