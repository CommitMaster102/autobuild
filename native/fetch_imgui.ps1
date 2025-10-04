# Script to download Dear ImGui for the autobuild GUI (Windows PowerShell)

$IMGUI_VERSION = "v1.90.1"
$IMGUI_DIR = "native/external/imgui"

New-Item -ItemType Directory -Force -Path $IMGUI_DIR | Out-Null

Write-Host "Downloading Dear ImGui $IMGUI_VERSION..." -ForegroundColor Green

$base_url = "https://raw.githubusercontent.com/ocornut/imgui/$IMGUI_VERSION"

$files = @(
    "imgui.cpp",
    "imgui.h",
    "imgui_draw.cpp",
    "imgui_tables.cpp",
    "imgui_widgets.cpp",
    "imgui_demo.cpp",
    "imgui_internal.h",
    "imconfig.h",
    "imstb_rectpack.h",
    "imstb_textedit.h",
    "imstb_truetype.h"
)

$backend_files = @(
    "backends/imgui_impl_sdl2.cpp",
    "backends/imgui_impl_sdl2.h",
    "backends/imgui_impl_sdlrenderer2.cpp",
    "backends/imgui_impl_sdlrenderer2.h"
)

foreach ($file in $files) {
    $url = "$base_url/$file"
    $output = "$IMGUI_DIR/$file"
    Write-Host "Downloading $file..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $output
}

foreach ($file in $backend_files) {
    $filename = Split-Path $file -Leaf
    $url = "$base_url/$file"
    $output = "$IMGUI_DIR/$filename"
    Write-Host "Downloading $filename..." -ForegroundColor Cyan
    Invoke-WebRequest -Uri $url -OutFile $output
}

Write-Host "`nDear ImGui downloaded successfully to $IMGUI_DIR" -ForegroundColor Green



