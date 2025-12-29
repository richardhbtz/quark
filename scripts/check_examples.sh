#!/bin/bash

# Script to check if all Quark examples compile and run
# Usage: ./check_examples.sh

set -e

# Default values
QUARK="build/quark"
EXAMPLES_DIR="examples"
OUT_DIR="examples/bin"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
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

# Change to script directory
cd "$(dirname "$0")/.."

# Check if Quark compiler exists
if [ ! -f "$QUARK" ]; then
    print_color "$RED" "Error: Quark compiler not found at $QUARK"
    exit 1
fi

# Create output directory
mkdir -p "$OUT_DIR"

# Initialize counters
total=0
passed=0
failed=0
failed_names=()

print_color "$CYAN" "========================================"
print_color "$CYAN" "       Quark Examples Check"
print_color "$CYAN" "========================================"
echo ""

# Find all example files
for example_file in "$EXAMPLES_DIR"/*.k; do
    [ -f "$example_file" ] || continue
    
    ((total++))
    name=$(basename "$example_file" .k)
    executable="$OUT_DIR/$name"
    
    # Remove any existing binary
    rm -f "$executable"
    
    printf "%-25s" "$name"
    
    # Compile the example
    compile_output=""
    if ! compile_output=$("$QUARK" -q -o "$executable" "$example_file" 2>&1); then
        print_color "$RED" "[COMPILE FAILED]"
        echo "  Error: $compile_output"
        ((failed++))
        failed_names+=("$name (compile)")
        continue
    fi
    
    # Check if executable was created
    if [ ! -f "$executable" ]; then
        print_color "$RED" "[COMPILE FAILED]"
        ((failed++))
        failed_names+=("$name (no binary)")
        continue
    fi
    
    # Run the example
    run_output=""
    exit_code=0
    run_output=$("$executable" 2>&1) || exit_code=$?
    
    if [ $exit_code -ne 0 ]; then
        print_color "$RED" "[RUNTIME ERROR]"
        echo "  Exit code: $exit_code"
        ((failed++))
        failed_names+=("$name (runtime)")
        continue
    fi
    
    print_color "$GREEN" "[OK]"
    ((passed++))
done

echo ""
print_color "$CYAN" "========================================"
print_color "$CYAN" "               Results"
print_color "$CYAN" "========================================"
echo ""
echo "Total:  $total"
print_color "$GREEN" "Passed: $passed"
if [ $failed -gt 0 ]; then
    print_color "$RED" "Failed: $failed"
    echo ""
    print_color "$RED" "Failed examples:"
    for name in "${failed_names[@]}"; do
        echo "  - $name"
    done
fi
echo ""

# Cleanup
rm -rf "$OUT_DIR"

# Exit with appropriate code
if [ $failed -gt 0 ]; then
    exit 1
fi
exit 0
