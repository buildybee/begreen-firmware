#!/bin/bash

# Function to extract version from objects.h
get_version_from_objects() {
    local objects_file="objects.h"
    if [ -f "$objects_file" ]; then
        # Extract version number between quotes
        grep -E 'FIRMWARE_VERSION' "$objects_file" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+'
    else
        echo ""
    fi
}

# Get default version from objects.h
DEFAULT_VERSION=$(get_version_from_objects)

# Check if arguments are provided
if [ $# -lt 1 ]; then
    echo "Usage: $0 <input_file.ino> [version_number (e.g., 1.0.0)]"
    if [ -n "$DEFAULT_VERSION" ]; then
        echo "Default version from objects.h: $DEFAULT_VERSION"
    fi
    exit 1
fi

# Check if first argument ends with .ino
if [[ ! "$1" =~ \.ino$ ]]; then
    echo "Error: First argument must be a .ino file"
    exit 1
fi

# Set version (use argument if provided, otherwise use default from objects.h)
if [ $# -ge 2 ]; then
    VERSION="$2"
else
    if [ -z "$DEFAULT_VERSION" ]; then
        echo "Error: No version provided and couldn't read from objects.h"
        exit 1
    fi
    VERSION="$DEFAULT_VERSION"
    echo "Using version from objects.h: $VERSION"
fi

# Check if version number is in correct format (X.Y.Z)
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Version number must be in format X.Y.Z (e.g., 1.0.0)"
    exit 1
fi

# Compile the Arduino sketch
echo "Compiling $1 with version $VERSION..."
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 --output-dir /home/saypaul/Arduino/beegreen_custom_ticker "$1"

# Check if compilation was successful
if [ $? -ne 0 ]; then
    echo "Error: Compilation failed"
    exit 1
fi

# Move the binary file
BINARY_DEST="/home/saypaul/git_repos/beegreen-firmware-upgrade/firmware/esp7ina219/$VERSION.bin"
echo "Moving binary to $BINARY_DEST"
mv "$1.bin" "$BINARY_DEST"

# Delete generated .elf and .map files
for ext in elf map; do
    if [ -f "$1.$ext" ]; then
        rm "$1.$ext"
        echo "Deleted $1.$ext"
    fi
done

# Update the version in the version file
VERSION_FILE="/home/saypaul/git_repos/beegreen-firmware-upgrade/esp7ina219.txt"
if [ -f "$VERSION_FILE" ]; then
    echo "$VERSION" > "$VERSION_FILE"
    echo "Version updated to $VERSION in $VERSION_FILE"
else
    echo "Error: Version file $VERSION_FILE not found"
    exit 1
fi

echo "Build and version update completed successfully, manually push the firmware upgrade"