#!/bin/bash
# Test script for macOS OpenGL animation
# This script tests the animation without launching the main app

echo "Testing Autobuild OpenGL Animation on macOS..."
echo ""

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if we're running from a built app bundle or development directory
if [ -f "$SCRIPT_DIR/native/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui" ]; then
    # Running from built app bundle
    ANIMATION_APP="$SCRIPT_DIR/native/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui"
elif [ -f "$SCRIPT_DIR/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui" ]; then
    # Running from build_macos directory
    ANIMATION_APP="$SCRIPT_DIR/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui"
else
    echo "Error: Could not find autobuild_gui executable"
    echo "Please build the project first using:"
    echo "  ./build_macos_complete.sh"
    exit 1
fi

# Check if executable exists
if [ ! -f "$ANIMATION_APP" ]; then
    echo "Error: Animation executable not found at: $ANIMATION_APP"
    exit 1
fi

echo "Found animation app: $ANIMATION_APP"
echo ""
echo "Starting animation test..."
echo "The animation should show a spinning cube for 3 seconds."
echo "You can press any key or click to skip the animation."
echo ""

# Launch the animation
"$ANIMATION_APP"

echo ""
echo "Animation test completed!"
echo "If you saw a spinning cube animation, the fix is working correctly."
