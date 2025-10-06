#!/bin/bash
# Complete macOS build and DMG creation script
# This script handles the entire process from build to DMG creation

set -e

echo " Building Autobuild for macOS..."

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo " This script must be run on macOS"
    exit 1
fi

# Check for required tools
command -v cmake >/dev/null 2>&1 || { echo " CMake is required but not installed. Install with: brew install cmake"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo " Python 3 is required but not installed."; exit 1; }

# Create background image if it doesn't exist
if [ ! -f "native/resources/dmg_background.png" ]; then
    echo " Creating DMG background image..."
    python3 create_dmg_background.py
fi

# Clean previous builds
echo " Cleaning previous builds..."
rm -rf build_macos
mkdir -p build_macos
cd build_macos

# Configure with CMake
echo "  Configuring with CMake..."
cmake -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ../native

# Build the project
echo " Building project..."
make -j$(sysctl -n hw.ncpu)

# Install to staging area
echo " Installing to staging area..."
make install DESTDIR=./install

# Create the DMG using CPack
echo " Creating DMG installer..."
cpack -G DragNDrop

# Find and display the created DMG
DMG_FILE=$(find . -name "*.dmg" -type f | head -1)
if [ -n "$DMG_FILE" ]; then
    echo " DMG created successfully: $DMG_FILE"
    echo " DMG size: $(du -h "$DMG_FILE" | cut -f1)"
    
    # Optional: Mount and test the DMG
    echo " Testing DMG..."
    hdiutil attach "$DMG_FILE" -readonly -nobrowse
    echo " DMG mounted successfully"
    echo " Contents:"
    ls -la "/Volumes/Autobuild/"
    hdiutil detach "/Volumes/Autobuild/"
    
    echo ""
    echo " Build completed successfully!"
    echo " DMG location: $(pwd)/$DMG_FILE"
else
    echo " DMG creation failed"
    exit 1
fi
