// Comprehensive map (dictionary/hashmap) example

int main() {
    println("=== Creating a map ===");
    var users = map_new();
    
    println("\n=== Adding entries ===");
    users.set("alice", "Engineer");
    users.set("bob", "Designer");
    users.set("charlie", "Manager");
    
    println("Map size: {}", users.len());
    
    println("\n=== Retrieving values ===");
    println("Alice's role: {}", users.get("alice"));
    println("Bob's role: {}", users.get("bob"));
    
    println("\n=== Checking existence ===");
    if (users.has("alice")) {
        println("Alice exists in map");
    }
    
    if (!users.has("david")) {
        println("David does not exist in map");
    }
    
    println("\n=== Updating values ===");
    users.set("alice", "Senior Engineer");
    println("Alice's new role: {}", users.get("alice"));
    
    println("\n=== Removing entries ===");
    if (users.remove("bob")) {
        println("Removed bob from map");
    }
    println("Map size after removal: {}", users.len());
    
    println("\n=== Using maps for configuration ===");
    var config = map_new();
    config.set("host", "localhost");
    config.set("port", "8080");
    config.set("protocol", "http");
    
    var url = config.get("protocol") + "://" + config.get("host") + ":" + config.get("port");
    println("URL: {}", url);
    
    println("\n=== Cleanup ===");
    users.free();
    config.free();
    println("Maps freed");
    
    ret 0;
}
