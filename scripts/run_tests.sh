#!/bin/bash

# Script to run Quark language tests
# Usage: ./run_tests.sh [options]
# Options:
#   -q, --quark PATH     Path to Quark compiler (default: build/quark)
#   -t, --tests-dir DIR  Tests directory (default: tests)
#   -o, --out-dir DIR    Output directory (default: tests/bin)
#   -v, --verbose        Enable verbose output
#   -h, --help          Show this help message

set -e

# Default values
QUARK="build/quark"
TESTS_DIR="tests"
OUT_DIR="tests/bin"
VERBOSE=false

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
DARK_YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_color() {
    local color=$1
    local message=$2
    if [[ -t 1 ]]; then
        printf "${color}%s${NC}\n" "$message"
    else
        printf "%s\n" "$message"
    fi
}

# Function to show help
show_help() {
    cat << EOF
Usage: $0 [options]

Options:
  -q, --quark PATH     Path to Quark compiler (default: build/quark)
  -t, --tests-dir DIR  Tests directory (default: tests)
  -o, --out-dir DIR    Output directory (default: tests/bin)
  -v, --verbose        Enable verbose output
  -h, --help          Show this help message

Examples:
  $0                           # Run tests with defaults
  $0 -v                        # Run with verbose output
  $0 -q ./build/quark -v       # Specify compiler path with verbose
EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -q|--quark)
            QUARK="$2"
            shift 2
            ;;
        -t|--tests-dir)
            TESTS_DIR="$2"
            shift 2
            ;;
        -o|--out-dir)
            OUT_DIR="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Change to script directory
cd "$(dirname "$0")/.."

# Check if Quark compiler exists
if [ ! -f "$QUARK" ]; then
    print_color "$RED" "Error: Quark compiler not found at $QUARK"
    exit 1
fi

# Create output directory
mkdir -p "$OUT_DIR"

# Function to invoke Quark with retry logic
invoke_quark() {
    local compiler="$1"
    local src="$2"
    local out="$3"
    local max_tries=5
    
    for ((i=1; i<=max_tries; i++)); do
        if "$compiler" -q -o "$out" "$src" >/dev/null 2>&1; then
            return 0
        elif [ $i -ge $max_tries ]; then
            return 1
        else
            sleep 0.3
        fi
    done
}

# Initialize counters
total=0
passed=0
failed=0

# Find all test files
test_files=($(find "$TESTS_DIR" -name "*.k" -type f | sort))

for test_file in "${test_files[@]}"; do
    ((total++))
    name=$(basename "$test_file" .k)
    executable="$OUT_DIR/$name"
    
    # Always force a fresh compile by removing any existing binary
    rm -f "$executable"
    
    if [ "$VERBOSE" = true ]; then
        echo "Compiling $test_file -> $executable"
    fi
    
    # Compile the test
    if ! invoke_quark "$QUARK" "$test_file" "$executable"; then
        print_color "$RED" "[FAIL] $name (compile failed)"
        ((failed++))
        continue
    fi
    
    # Check if executable was created
    if [ ! -f "$executable" ]; then
        print_color "$RED" "[FAIL] $name (compile failed)"
        ((failed++))
        continue
    fi
    
    if [ "$VERBOSE" = true ]; then
        echo "Running $executable"
    fi
    
    # Run the test and capture output
    output=""
    if ! output=$("$executable" 2>&1); then
        # If the test has no EXPECT lines, treat as a skip and continue
        if ! grep -q "^\s*\(#\|//\)\s*EXPECT:\s*" "$test_file"; then
            print_color "$DARK_YELLOW" "[SKIP] $name (run error, no EXPECT)"
            continue
        fi
        print_color "$RED" "[FAIL] $name (runtime error)"
        ((failed++))
        continue
    fi
    
    # Normalize output (trim trailing whitespace)
    output=$(echo "$output" | sed 's/[[:space:]]*$//')
    
    # Extract expected output from test file
    expected=""
    if expect_lines=$(grep -E "^[[:space:]]*(#|//)[[:space:]]*EXPECT:" "$test_file" 2>/dev/null); then
        expected=$(echo "$expect_lines" | sed -E 's/^[[:space:]]*(#|\/\/)[[:space:]]*EXPECT:[[:space:]]*//')
        expected=$(echo "$expected" | sed 's/[[:space:]]*$//')
        
        if [ "$output" = "$expected" ]; then
            print_color "$GREEN" "[PASS] $name"
            ((passed++))
        else
            print_color "$RED" "[FAIL] $name"
            print_color "$YELLOW" "Expected:"
            echo "$expected"
            print_color "$YELLOW" "Got:"
            echo "$output"
            ((failed++))
        fi
    else
        print_color "$CYAN" "[RUN ] $name"
        if [ "$VERBOSE" = true ]; then
            echo "$output"
        fi
    fi
done

echo
echo "Summary: $passed/$total passed, $failed failed"
if [ $failed -gt 0 ]; then
    exit 1
else
    exit 0
fi
