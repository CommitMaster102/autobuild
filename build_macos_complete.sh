#!/bin/bash
# Complete macOS build and DMG creation script
# This script handles the entire process from build to DMG creation
# Note: Uses SDL2 for cross-platform compatibility (no Objective-C required)

set -e

echo " Building Autobuild for macOS with cross-platform compatibility..."

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo " This script must be run on macOS"
    exit 1
fi

# Check for required tools
command -v cmake >/dev/null 2>&1 || { echo " CMake is required but not installed. Install with: brew install cmake"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo " Python 3 is required but not installed."; exit 1; }

# Check for SDL2
if ! pkg-config --exists sdl2; then
    echo " SDL2 is required but not installed. Install with: brew install sdl2"
    echo "This is needed for the GUI applications to work."
    exit 1
fi

# Check for OpenGL (usually available on macOS)
if ! pkg-config --exists opengl; then
    echo "  OpenGL is required but not found. This is unusual on macOS."
    echo "OpenGL should be available by default on macOS systems."
fi

# Create background image if it doesn't exist
if [ ! -f "native/resources/dmg_background.png" ]; then
    echo " Creating DMG background image..."
    python3 create_dmg_background.py
fi

# Detect SDL2 installation path and choose architectures that match it
SDL2_PATH=""
SDL2_PREFIX=""
SDL2_LIBDIR="$(pkg-config --variable=libdir sdl2 2>/dev/null || echo "")"
if [ -n "$SDL2_LIBDIR" ] && [ -f "$SDL2_LIBDIR/libSDL2.dylib" ]; then
    SDL2_PATH="$SDL2_LIBDIR/libSDL2.dylib"
    SDL2_PREFIX="$(pkg-config --variable=prefix sdl2 2>/dev/null || echo "")"
fi

# Fallback search if pkg-config didn't yield a path
if [ -z "$SDL2_PATH" ]; then
    if [ -f "/opt/homebrew/lib/libSDL2.dylib" ]; then
        SDL2_PATH="/opt/homebrew/lib/libSDL2.dylib"
    elif [ -f "/usr/local/lib/libSDL2.dylib" ]; then
        SDL2_PATH="/usr/local/lib/libSDL2.dylib"
    else
        SDL2_PATH=$(find /opt/homebrew /usr/local -name "libSDL2.dylib" 2>/dev/null | head -1)
    fi
fi

# Compute architectures
MACOS_ARCHS=""
if [ -n "$SDL2_PATH" ] && [ -f "$SDL2_PATH" ]; then
    INFO=$(lipo -info "$SDL2_PATH" 2>/dev/null || true)
    if echo "$INFO" | grep -q "are:"; then
        # Fat binary; prefer universal build
        if echo "$INFO" | grep -q "x86_64" && echo "$INFO" | grep -q "arm64"; then
            MACOS_ARCHS="x86_64;arm64"
        elif echo "$INFO" | grep -q "x86_64"; then
            MACOS_ARCHS="x86_64"
        elif echo "$INFO" | grep -q "arm64"; then
            MACOS_ARCHS="arm64"
        fi
    else
        # Non-fat file line like: "Non-fat file: ... is architecture: arm64"
        if echo "$INFO" | grep -q "x86_64"; then
            MACOS_ARCHS="x86_64"
        else
            MACOS_ARCHS="arm64"
        fi
    fi
fi

# Final fallback: host machine arch
if [ -z "$MACOS_ARCHS" ]; then
    MACHINE=$(uname -m)
    case "$MACHINE" in
        x86_64) MACOS_ARCHS="x86_64" ;;
        arm64|aarch64) MACOS_ARCHS="arm64" ;;
        *) MACOS_ARCHS="arm64" ;;
    esac
fi

echo " Using SDL2 at: ${SDL2_PATH:-unknown}"
echo " Building architectures: $MACOS_ARCHS"

# Clean previous builds
echo " Cleaning previous builds..."
rm -rf build_macos
mkdir -p build_macos
cd build_macos

# Configure with CMake - match architectures to installed SDL2
echo "  Configuring with CMake..."
cmake -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$MACOS_ARCHS" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ${SDL2_PREFIX:+-DCMAKE_PREFIX_PATH=$SDL2_PREFIX} \
      ../native

# Build the project
echo " Building project..."
make -j$(sysctl -n hw.ncpu)

# Install to staging area
echo " Installing to staging area..."
make install DESTDIR=./install

# Bundle SDL2 with the app (make it self-contained)
echo " Bundling SDL2 with app..."
# SDL2_PATH may have been determined above; otherwise fall back to search
if [ -z "$SDL2_PATH" ] || [ ! -f "$SDL2_PATH" ]; then
    if [ -f "/opt/homebrew/lib/libSDL2.dylib" ]; then
        SDL2_PATH="/opt/homebrew/lib/libSDL2.dylib"
    elif [ -f "/usr/local/lib/libSDL2.dylib" ]; then
        SDL2_PATH="/usr/local/lib/libSDL2.dylib"
    else
        SDL2_PATH=$(find /opt/homebrew /usr/local -name "libSDL2.dylib" 2>/dev/null | head -1)
    fi
fi

if [ -n "$SDL2_PATH" ] && [ -f "$SDL2_PATH" ]; then
    # Find the app bundle
    APP_BUNDLE=$(find ./install -name "*.app" -type d | head -1)
    if [ -n "$APP_BUNDLE" ]; then
        # Create Frameworks directory
        mkdir -p "$APP_BUNDLE/Contents/Frameworks"
        
        # Copy SDL2 library
        cp "$SDL2_PATH" "$APP_BUNDLE/Contents/Frameworks/"
        
        # Update library paths in the executable
        EXECUTABLE="$APP_BUNDLE/Contents/MacOS/$(basename "$APP_BUNDLE" .app)"
        if [ -f "$EXECUTABLE" ]; then
            install_name_tool -change "$SDL2_PATH" "@executable_path/../Frameworks/libSDL2.dylib" "$EXECUTABLE"
            echo " SDL2 bundled successfully"
        fi
    else
        echo "  Warning: App bundle not found, SDL2 not bundled"
    fi
else
    echo "  Warning: SDL2 library not found, app may not work on other Macs"
fi

# Create the DMG using CPack (package from current build tree)
echo " Creating DMG installer..."
cpack -G DragNDrop -B .

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
    echo ""
    echo " Your macOS DMG is ready for distribution!"
    echo "   Users can simply drag the app to Applications folder."
    echo ""
    echo " To test the animation, run:"
    echo "   ./launch_with_animation_macos.sh"
else
    echo " DMG creation failed"
    exit 1
fi
