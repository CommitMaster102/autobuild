@echo off
echo Starting Autobuild 2.0 with assertions disabled...
echo This will prevent ImGui assertion dialog boxes from appearing.
echo.
native\build-gui\autobuild_gui.exe --no-assert
pause
