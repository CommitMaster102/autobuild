#!/bin/bash
# Debug script for macOS OpenGL animation issues
# This script helps diagnose why the animation might not be working

echo "=== Autobuild Animation Debug Script for macOS ==="
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
echo "Script directory: $SCRIPT_DIR"

# Check for animation executable in various locations
echo ""
echo "=== Checking for Animation Executable ==="

ANIMATION_PATHS=(
    "$SCRIPT_DIR/native/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui"
    "$SCRIPT_DIR/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui"
    "$SCRIPT_DIR/native/build-gui-mingw/autobuild_gui.exe"
)

for path in "${ANIMATION_PATHS[@]}"; do
    if [ -f "$path" ]; then
        echo "✓ Found animation executable: $path"
        ANIMATION_APP="$path"
        break
    else
        echo "✗ Not found: $path"
    fi
done

if [ -z "$ANIMATION_APP" ]; then
    echo ""
    echo "ERROR: No animation executable found!"
    echo "Please build the project first using:"
    echo "  ./build_macos_complete.sh"
    exit 1
fi

echo ""
echo "=== Checking System Requirements ==="

# Check for SDL2
echo "Checking SDL2..."
if pkg-config --exists sdl2; then
    echo "✓ SDL2 found via pkg-config"
    SDL2_VERSION=$(pkg-config --modversion sdl2)
    echo "  Version: $SDL2_VERSION"
else
    echo "✗ SDL2 not found via pkg-config"
    echo "  Install with: brew install sdl2"
fi

# Check for OpenGL
echo ""
echo "Checking OpenGL..."
if pkg-config --exists opengl; then
    echo "✓ OpenGL found via pkg-config"
else
    echo "✗ OpenGL not found via pkg-config"
    echo "  This is unusual on macOS - OpenGL should be available by default"
fi

# Check for shader files
echo ""
echo "=== Checking Shader Files ==="
SHADER_PATHS=(
    "$SCRIPT_DIR/native/apps/vertex.glsl"
    "$SCRIPT_DIR/native/apps/fragment.glsl"
    "$SCRIPT_DIR/build_macos/vertex.glsl"
    "$SCRIPT_DIR/build_macos/fragment.glsl"
)

for shader in "${SHADER_PATHS[@]}"; do
    if [ -f "$shader" ]; then
        echo "✓ Found shader: $shader"
    else
        echo "✗ Missing shader: $shader"
    fi
done

# Check for main application
echo ""
echo "=== Checking Main Application ==="
MAIN_PATHS=(
    "$SCRIPT_DIR/native/build_macos/autobuild_main.app/Contents/MacOS/autobuild_main"
    "$SCRIPT_DIR/build_macos/autobuild_main.app/Contents/MacOS/autobuild_main"
)

for path in "${MAIN_PATHS[@]}"; do
    if [ -f "$path" ]; then
        echo "✓ Found main app: $path"
        MAIN_APP="$path"
        break
    else
        echo "✗ Not found: $path"
    fi
done

echo ""
echo "=== Testing Animation Executable ==="
echo "Running animation with debug output..."
echo ""

# Run the animation with debug output
if [[ "$ANIMATION_APP" == *.exe ]]; then
    echo "Windows executable detected, trying with Wine..."
    if command -v wine >/dev/null 2>&1; then
        wine "$ANIMATION_APP"
    else
        echo "Wine not found. Please install Wine to run Windows executable on macOS."
    fi
else
    echo "Running macOS animation executable..."
    "$ANIMATION_APP"
fi

echo ""
echo "=== Debug Complete ==="
echo "If you saw debug output above, it should help identify the issue."
echo "Common issues:"
echo "  1. Missing SDL2 - install with: brew install sdl2"
echo "  2. Missing shader files - check if they're copied during build"
echo "  3. OpenGL context creation failed - check macOS OpenGL support"
echo "  4. Animation window not visible - check if it's behind other windows"
