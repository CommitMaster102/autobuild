@echo off
echo Starting Autobuild 2.0 in Debug Mode...
echo This will show all debug information in the console.
echo ImGui assertions will be caught and logged instead of showing dialog boxes.
echo Press Ctrl+C to stop.
echo.
native\build-gui\autobuild_gui.exe --debug --no-assert
pause
