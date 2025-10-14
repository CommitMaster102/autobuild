@echo off
echo === Random Shape Animation Test ===
echo.

echo 1. Checking build directory...
if not exist "build" (
    echo    X Build directory not found. Please run cmake and make first.
    echo    mkdir build ^&^& cd build
    echo    cmake .. ^&^& make
    pause
    exit /b 1
)

cd build

echo    ✓ Build directory found

echo.
echo 2. Checking for autobuild_gui executable...
if not exist "autobuild_gui.exe" (
    echo    X autobuild_gui.exe not found
    echo    Please build the project first
    pause
    exit /b 1
)

echo    ✓ autobuild_gui.exe found

echo.
echo 3. Running random shape animation test...
echo    The animation will show different geometric shapes:
echo    - Cube (original)
echo    - Tetrahedron (4-sided pyramid)
echo    - Octahedron (8-sided diamond)
echo    - Icosahedron (20-sided sphere)
echo    - Torus (donut shape)
echo    - Sphere (smooth sphere)
echo    - Pyramid (square pyramid)
echo    - Diamond (complex diamond shape)
echo.
echo    One random shape will be selected and displayed for the entire 5-second animation.
echo    Press any key or click to skip the animation.
echo.

echo 4. Building with MinGW compatibility fixes...
cd native
cmake --build build-gui-mingw --target autobuild_gui
if errorlevel 1 (
    echo    X Build failed! Check the error messages above.
    pause
    exit /b 1
)
echo    ✓ Build successful!

echo.
echo 5. Starting animation...
cd build-gui-mingw
autobuild_gui.exe

echo.
echo 6. Test completed!
echo.
echo If you saw a single shape rotating for 5 seconds, the random shape system is working!
echo Each time you run this, you should see a different random shape selected.
