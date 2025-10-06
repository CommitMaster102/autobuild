#!/bin/bash
# Build script for macOS DMG creation
# Run this on macOS to create the DMG installer

set -e

echo "Building Autobuild for macOS..."

# Clean previous builds
rm -rf build_macos
mkdir -p build_macos
cd build_macos

# Configure with CMake
cmake -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
      ../native

# Build the project
make -j$(sysctl -n hw.ncpu)

# Create the DMG using CPack
echo "Creating DMG installer..."
cpack -G DragNDrop

echo "DMG created successfully!"
ls -la *.dmg
