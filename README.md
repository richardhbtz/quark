<div align="center">

<picture>
      <img src="assets/quark-icon.png" width="200" />
</picture>

# Quark

A **fast, modern compiled language** with **C and Rust-like syntax**

</div>

## ⚡️ Quick Start

```bash
# Clone the repository
git clone https://github.com/richardhbtz/quark.git
cd quark

# Build the compiler
cmake -S . -B build && cmake --build build

# Compile and run a Quark program
./build/quark run examples/hello.k
```

## 📖 Language Overview

```quark
// Variable declaration with type inference
var x = 42;
int explicit_int = 10;

// Functions
int add(a: int, b: int) {
    ret a + b;
}

// Structs with methods
struct Point {
    data { x: int, y: int }
    
    Point new(x: int, y: int) {
        ret Point { x: x, y: y };
    }
    
    int sum() {
        ret x + y;
    }
}

var p = Point.new(1, 2);  // Static method call

// Inheritance
struct Child : Parent {
    data { extra: int }
}

// C interop
extern "C" { int abs(int); }

// Control flow
for (i in 0..5) { print("{}", i); }
while (condition) { /* ... */ }
if (x > 0) { /* ... */ } elif (x < 0) { /* ... */ } else { /* ... */ }

// Arrays
var arr = [1, 2, 3];
print("{}", arr.length());
var slice = arr.slice(0, 2, 1);

// Maps (dictionaries/hashmaps)
map m;
m.set("key", "value");
var val = m.get("key");
if (m.contains("key")) { /* ... */ }
m.free();

// Map literal syntax
map m2 = { "name": "Alice", "age": "30" };

// Pointers
int* ptr = &x;
*ptr = 10;
```

## ⚙️ Building

### Prerequisites

| Platform | Requirements |
| --- | --- |
| **macOS** | Homebrew, LLVM, FTXUI, CURL, IXWebSocket |
| **Windows** | Visual Studio 2022, vcpkg dependencies |
| **Linux** | LLVM, LLD, FTXUI, CURL |

### Build Commands

**macOS / Linux:**
```bash
cmake -S . -B build && cmake --build build
```

**Windows (PowerShell):**
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Running Tests

```bash
cmake --build build --target quark_tests
./build/quark_tests
```

## 📦 Package Manager

Quark includes a built-in package manager inspired by Cargo.

```bash
# Create a new project
quark package init my-project

# Build and run
quark package build
quark package run

# Build with release optimizations
quark package build --release
```

### Manifest Example (`Quark.toml`)

```toml
[package]
name = "my_project"
version = "0.1.0"
description = "A Quark project"
license = "MIT"
authors = ["Your Name <you@example.com>"]
repository = "https://github.com/user/my_project"

[build]
target = "executable"
source_files = [ "src/main.k" ]
library_paths = [ "lib/" ]         # Directories to search for native libraries
link_libraries = [ "mylib" ]       # Native libraries to link (e.g., links mylib)

[dependencies]
some-module = { git = "https://github.com/user/some-module" }

[profile.release.optimization]
enabled = true
level = 3
lto = true
```

## 🛠️ Built With

- [LLVM](https://llvm.org/) - Compiler infrastructure for code generation
- [LLD](https://lld.llvm.org/) - LLVM linker for native binary linking
- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) - Terminal UI for error display
- [CURL](https://curl.se/) - HTTP client library
- [IXWebSocket](https://github.com/machinezone/IXWebSocket) - WebSocket support

## 📚 Runtime Libraries

| Library | Description |
| --- | --- |
| `quark_http` | CURL-based HTTP client |
| `quark_json` | JSON parsing and writing |
| `quark_ws` | WebSocket support via IXWebSocket |
| `quark_toml` | TOML configuration parsing |
| `quark_io` | File and console helpers |
| `quark_runtime` | Native runtime (built-in list/map types) |

## � Examples

The `examples/` folder contains sample programs demonstrating Quark's features:

| Example | Description |
| --- | --- |
| [`hello.k`](examples/hello.k) | Hello World - your first Quark program |
| [`variables.k`](examples/variables.k) | Variables, types, and type inference |
| [`functions.k`](examples/functions.k) | Functions, parameters, and recursion |
| [`control_flow.k`](examples/control_flow.k) | If/elif/else, for loops, while loops, break/continue |
| [`arrays.k`](examples/arrays.k) | Array creation, indexing, slicing, and iteration |
| [`strings.k`](examples/strings.k) | String operations, concatenation, and methods |
| [`structs.k`](examples/structs.k) | Structs with data fields and methods |
| [`inheritance.k`](examples/inheritance.k) | Struct inheritance and method overriding |
| [`pointers.k`](examples/pointers.k) | Pointer declaration, dereferencing, and manipulation |
| [`c_interop.k`](examples/c_interop.k) | Calling C library functions |
| [`math.k`](examples/math.k) | Math builtins and type casting |
| [`map.k`](examples/map.k) | Maps (dictionaries/hashmaps) with key-value storage |
| [`http_request.k`](examples/http_request.k) | HTTP requests with the built-in HTTP library |
| [`json_parsing.k`](examples/json_parsing.k) | JSON parsing with the built-in JSON library |

Run any example:
```bash
./build/quark run examples/hello.k
```

## �📜 License

This project is licensed under the MIT License. See the [LICENSE](./LICENSE) file for details.
