#!/bin/bash
# Create a PKG installer for macOS (alternative to DMG)
# This creates a traditional installer package

set -e

echo "📦 Creating PKG installer for macOS..."

# Check if we're on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo "❌ This script must be run on macOS"
    exit 1
fi

# Build the project first
if [ ! -d "build_macos" ]; then
    echo "🔨 Building project first..."
    ./build_macos_complete.sh
fi

cd build_macos

# Create a package using pkgbuild
echo "📦 Creating PKG installer..."

# Create a temporary directory for the package contents
PKG_ROOT="pkg_root"
rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/Applications"

# Copy the app bundle
if [ -d "install/usr/local/bin/Autobuild.app" ]; then
    cp -R "install/usr/local/bin/Autobuild.app" "$PKG_ROOT/Applications/"
elif [ -d "install/Applications/Autobuild.app" ]; then
    cp -R "install/Applications/Autobuild.app" "$PKG_ROOT/Applications/"
else
    echo "❌ App bundle not found. Make sure the build completed successfully."
    exit 1
fi

# Create package info
PKG_ID="com.autobuild.gui"
PKG_VERSION="2.0.0"
PKG_NAME="Autobuild-${PKG_VERSION}.pkg"

# Build the package
pkgbuild --root "$PKG_ROOT" \
         --identifier "$PKG_ID" \
         --version "$PKG_VERSION" \
         --install-location "/" \
         "$PKG_NAME"

echo "✅ PKG installer created: $PKG_NAME"
echo "📊 Package size: $(du -h "$PKG_NAME" | cut -f1)"

# Clean up
rm -rf "$PKG_ROOT"

echo "🎉 PKG installer creation completed!"
echo "📁 PKG location: $(pwd)/$PKG_NAME"
