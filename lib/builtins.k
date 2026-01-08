// Quark Built-in Functions
// This file provides type signatures for built-in functions for code completion and type checking
// All built-in functions are implemented in the compiler and available without imports

// =============================================================================
// I/O Functions
// =============================================================================

// Prints values to stdout with automatic formatting. Supports variadic arguments.
// Examples:
//   print();                  // prints newline
//   print("Hello");           // prints: Hello
//   print("x = {}", 42);      // prints: x = 42
void print(...);

// Reads a line from stdin. Optional prompt can be provided.
// Examples:
//   var name = readline();
//   var age = readline("Enter age: ");
str readline(...);

// Formats a string with placeholder substitution. Use {} for placeholders.
// Example:
//   var msg = format("Hello {}, you are {} years old", name, 25);
str format(...);

// =============================================================================
// Type Conversion Functions
// =============================================================================

// Converts any value to a string representation
// Supported types: int, float, double, bool, str
// Example:
//   var s = to_string(42);        // "42"
//   var s = to_string(3.14);      // "3.14"
//   var s = to_string(true);      // "true"
str to_string(...);

// JavaScript-style alias for to_string
str toString(...);

// Converts a value to an integer
// Supported types: int, float, double, bool, str
// Examples:
//   var n = to_int("123");        // 123
//   var n = to_int(3.14);         // 3
//   var n = to_int(true);         // 1
int to_int(...);

// Alias for to_int - parses a string to an integer
int parse_int(...);

// JavaScript-style alias for to_int
int parseInt(...);

// =============================================================================
// String Functions
// =============================================================================

// Concatenates two strings and returns a new string
// Example:
//   var greeting = str_concat("Hello", " World");  // "Hello World"
str str_concat(a: str, b: str);

// Extracts a substring from start index (inclusive) to end index (exclusive)
// Example:
//   var sub = str_slice("Hello", 1, 4);  // "ell"
str str_slice(s: str, start: int, end: int);

// Searches for needle in haystack, returns true if found
// Example:
//   var found = str_find("Hello World", "World");  // true
bool str_find(haystack: str, needle: str);

// Replaces all occurrences of old substring with new substring
// Example:
//   var s = str_replace("Hello World", "World", "Quark");  // "Hello Quark"
str str_replace(s: str, old: str, new: str);

// Splits a string by delimiter and returns an array of strings
// Example:
//   var parts = str_split("a,b,c", ",");  // ["a", "b", "c"]
str[] str_split(s: str, delim: str);

// Returns the length of a string in bytes
// Example:
//   var len = str_len("Hello");  // 5
int str_len(s: str);

// Alias for str_len - returns the length of a string
int str_length(s: str);

// Checks if a string starts with the given prefix
// Example:
//   var result = str_starts_with("Hello", "He");  // true
bool str_starts_with(s: str, prefix: str);

// Checks if a string ends with the given suffix
// Example:
//   var result = str_ends_with("Hello", "lo");  // true
bool str_ends_with(s: str, suffix: str);

// =============================================================================
// Math Functions - Trigonometric
// =============================================================================

// Sine function (input in radians)
double sin(x: double);

// Cosine function (input in radians)
double cos(x: double);

// Tangent function (input in radians)
double tan(x: double);

// Arc sine (returns radians)
double asin(x: double);

// Arc cosine (returns radians)
double acos(x: double);

// Arc tangent (returns radians)
double atan(x: double);

// Two-argument arc tangent (returns radians)
double atan2(y: double, x: double);

// Hyperbolic sine
double sinh(x: double);

// Hyperbolic cosine
double cosh(x: double);

// Hyperbolic tangent
double tanh(x: double);

// =============================================================================
// Math Functions - Basic Operations
// =============================================================================

// Square root
double sqrt(x: double);

// Power: base raised to exp
double pow(base: double, exp: double);

// Natural logarithm (base e)
double log(x: double);

// Base-10 logarithm
double log10(x: double);

// Exponential function (e^x)
double exp(x: double);

// Absolute value (floating-point)
double abs(x: double);

// Floor function (rounds down to nearest integer)
double floor(x: double);

// Ceiling function (rounds up to nearest integer)
double ceil(x: double);

// Rounds to nearest integer
double round(x: double);

// Floating-point remainder of x/y
double fmod(x: double, y: double);

// =============================================================================
// Math Functions - Integer/Float Specific
// =============================================================================

// Absolute value for 32-bit integers
int abs_i32(x: int);

// Absolute value for 64-bit floats
double abs_f64(x: double);

// Minimum of two 32-bit integers
int min_i32(a: int, b: int);

// Maximum of two 32-bit integers
int max_i32(a: int, b: int);

// Minimum of two 64-bit floats
double min_f64(a: double, b: double);

// Maximum of two 64-bit floats
double max_f64(a: double, b: double);

// Variadic minimum (works with int or double)
double min(...);

// Variadic maximum (works with int or double)
double max(...);

// Clamps an integer value between lo and hi
int clamp_i32(x: int, lo: int, hi: int);

// Clamps a float value between lo and hi
double clamp_f64(x: double, lo: double, hi: double);

// Variadic clamp (works with int or double)
double clamp(...);

// =============================================================================
// Memory Management Functions
// =============================================================================

// Allocates size bytes of memory and returns a pointer
// Example:
//   var ptr = alloc(1024);  // allocate 1KB
void* alloc(size: int);

// Frees memory previously allocated with alloc
// Example:
//   free(ptr);
void free(ptr: void*);

// Reallocates memory to a new size
// Example:
//   ptr = realloc(ptr, 2048);  // resize to 2KB
void* realloc(ptr: void*, new_size: int);

// Sets size bytes of memory at ptr to value
// Example:
//   memset(buffer, 0, 256);  // zero out 256 bytes
void* memset(ptr: void*, value: int, size: int);

// Copies size bytes from src to dest
// Example:
//   memcpy(dest, src, 100);
void* memcpy(dest: void*, src: void*, size: int);

// =============================================================================
// Array Functions (Internal - Used by Compiler)
// =============================================================================

// Returns the length of a dynamically-allocated array
// int array_length(arr: void*);

// Creates a slice of an array from start to end
// void* array_slice(arr: void*, start: int, end: int, stride: int);

// Pushes an element to the end of an array
// void* array_push(arr: void*, element: void*, element_size: int);

// Pops an element from the end of an array
// void* array_pop(arr: void*, element_size: int);

// Frees a dynamically-allocated array
// void array_free(arr: void*);

// =============================================================================
// Utility Functions
// =============================================================================

// Pauses execution for the specified number of milliseconds
// Cross-platform sleep function
// Example:
//   sleep(1000);  // sleep for 1 second
void sleep(ms: int);
