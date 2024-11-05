#!/bin/bash

if [ $# -eq 0 ]; then
    echo "Usage: $0 <program> [args...]"
    exit 1
fi

# Get the directory of this script
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_DIR="$( cd "$SCRIPT_DIR/.." && pwd )"

# Check if read_tracer.so exists
if [ ! -f "$PROJECT_DIR/build/libread_tracer.so" ]; then
    echo "Error: libread_tracer.so not found. Did you build the project?"
    exit 1
fi

# Run the program with our preload library
LD_PRELOAD="$PROJECT_DIR/build/libread_tracer.so" "$@"
