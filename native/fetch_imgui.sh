#!/usr/bin/env bash
# Script to download Dear ImGui for the autobuild GUI

IMGUI_VERSION="v1.90.1"
IMGUI_DIR="native/external/imgui"

mkdir -p "$IMGUI_DIR"

echo "Downloading Dear ImGui $IMGUI_VERSION..."

# Core ImGui files
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imgui.cpp" -o "$IMGUI_DIR/imgui.cpp"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imgui.h" -o "$IMGUI_DIR/imgui.h"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imgui_draw.cpp" -o "$IMGUI_DIR/imgui_draw.cpp"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imgui_tables.cpp" -o "$IMGUI_DIR/imgui_tables.cpp"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imgui_widgets.cpp" -o "$IMGUI_DIR/imgui_widgets.cpp"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imgui_internal.h" -o "$IMGUI_DIR/imgui_internal.h"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imconfig.h" -o "$IMGUI_DIR/imconfig.h"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imstb_rectpack.h" -o "$IMGUI_DIR/imstb_rectpack.h"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imstb_textedit.h" -o "$IMGUI_DIR/imstb_textedit.h"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/imstb_truetype.h" -o "$IMGUI_DIR/imstb_truetype.h"

# SDL2 backend
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/backends/imgui_impl_sdl2.cpp" -o "$IMGUI_DIR/imgui_impl_sdl2.cpp"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/backends/imgui_impl_sdl2.h" -o "$IMGUI_DIR/imgui_impl_sdl2.h"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/backends/imgui_impl_sdlrenderer2.cpp" -o "$IMGUI_DIR/imgui_impl_sdlrenderer2.cpp"
curl -L "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION/backends/imgui_impl_sdlrenderer2.h" -o "$IMGUI_DIR/imgui_impl_sdlrenderer2.h"

echo "Dear ImGui downloaded successfully to $IMGUI_DIR"

