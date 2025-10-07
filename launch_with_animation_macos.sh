#!/bin/bash
# macOS launcher script that runs OpenGL animation first, then main application
# This ensures the animation is displayed before the main GUI

echo "Starting Autobuild with OpenGL Animation..."

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if we're running from a built app bundle or development directory
if [ -f "$SCRIPT_DIR/native/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui" ]; then
    # Running from built app bundle
    ANIMATION_APP="$SCRIPT_DIR/native/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui"
    MAIN_APP="$SCRIPT_DIR/native/build_macos/autobuild_main.app/Contents/MacOS/autobuild_main"
elif [ -f "$SCRIPT_DIR/native/build-gui-mingw/autobuild_gui.exe" ]; then
    # Running from Windows build (for testing)
    echo "Windows build detected, launching with Wine if available..."
    if command -v wine >/dev/null 2>&1; then
        wine "$SCRIPT_DIR/native/build-gui-mingw/autobuild_gui.exe"
    else
        echo "Wine not found. Please install Wine to run Windows build on macOS."
        exit 1
    fi
    exit 0
elif [ -f "$SCRIPT_DIR/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui" ]; then
    # Running from build_macos directory
    ANIMATION_APP="$SCRIPT_DIR/build_macos/autobuild_gui.app/Contents/MacOS/autobuild_gui"
    MAIN_APP="$SCRIPT_DIR/build_macos/autobuild_main.app/Contents/MacOS/autobuild_main"
else
    echo "Error: Could not find autobuild_gui executable"
    echo "Please build the project first using:"
    echo "  ./build_macos_complete.sh"
    exit 1
fi

# Check if executables exist
if [ ! -f "$ANIMATION_APP" ]; then
    echo "Error: Animation executable not found at: $ANIMATION_APP"
    exit 1
fi

if [ ! -f "$MAIN_APP" ]; then
    echo "Error: Main application executable not found at: $MAIN_APP"
    exit 1
fi

echo "Found animation app: $ANIMATION_APP"
echo "Found main app: $MAIN_APP"

# Launch the animation first
echo "Launching OpenGL animation..."
"$ANIMATION_APP"

# The animation will automatically launch the main app when it finishes
echo "Animation completed, main application should be launching..."

# Optional: Add a small delay to ensure the animation has time to launch the main app
sleep 1

echo "Launch script completed."
