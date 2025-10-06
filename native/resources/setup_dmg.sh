#!/bin/bash
# Enhanced setup script for macOS DMG
# This script configures the DMG window appearance and layout

# Create Applications symlink
ln -sf /Applications /Volumes/Autobuild/Applications

# Set DMG window properties with better layout
osascript <<EOF
tell application "Finder"
    tell disk "Autobuild"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {400, 100, 900, 450}
        set theViewOptions to the icon view options of container window
        set arrangement of theViewOptions to not arranged
        set icon size of theViewOptions to 128
        set background picture of theViewOptions to file ".background:dmg_background.png"
        
        # Create Applications alias
        make new alias file at container window to POSIX file "/Applications" with properties {name:"Applications"}
        
        # Position items nicely
        set position of item "Autobuild.app" of container window to {100, 200}
        set position of item "Applications" of container window to {300, 200}
        
        # Set window properties
        set viewOptions to the icon view options of container window
        set text size of viewOptions to 12
        set label position of viewOptions to bottom
        
        close
        open
        update without registering applications
        delay 2
    end tell
end tell
EOF

echo "DMG setup completed successfully"
