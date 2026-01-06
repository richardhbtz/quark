// Comprehensive map (dictionary/hashmap) example

int main() {
    print("=== Creating a map ===");
    map users;
    
    print("\n=== Adding entries ===");
    users.set("alice", "Engineer");
    users.set("bob", "Designer");
    users.set("charlie", "Manager");
    
    print("Map size: {}", users.len());
    
    print("\n=== Retrieving values ===");
    print("Alice's role: {}", users.get("alice"));
    print("Bob's role: {}", users.get("bob"));
    
    print("\n=== Checking existence ===");
    if (users.contains("alice")) {
        print("Alice exists in map");
    }
    
    if (!users.contains("david")) {
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
    map config;
    config.set("host", "localhost");
    config.set("port", "8080");
    config.set("protocol", "http");
    
    var url = config.get("protocol") + "://" + config.get("host") + ":" + config.get("port");
    print("URL: {}", url);
    
    print("\n=== Map with initializer ===");
    map settings = {
        "theme": "dark",
        "language": "en",
        "timeout": "30"
    };
    print("Theme: {}", settings.get("theme"));
    print("\nLanguage: {}", settings.get("language"));
    
    ret 0;
}
