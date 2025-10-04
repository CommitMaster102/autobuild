@echo off
echo Creating Autobuild shortcuts...

set "INSTALL_DIR=%~dp0"
set "DESKTOP=%USERPROFILE%\Desktop"
set "START_MENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs"

REM Create desktop shortcut
echo Creating desktop shortcut...
powershell -Command "$WshShell = New-Object -comObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%DESKTOP%\Autobuild.lnk'); $Shortcut.TargetPath = '%INSTALL_DIR%autobuild_main.exe'; $Shortcut.Description = 'Autobuild - AI-Powered Build System'; $Shortcut.IconLocation = '%INSTALL_DIR%autobuild_main.exe,0'; $Shortcut.Save()"

REM Create Start Menu shortcut
echo Creating Start Menu shortcut...
if not exist "%START_MENU%\Autobuild" mkdir "%START_MENU%\Autobuild"
powershell -Command "$WshShell = New-Object -comObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%START_MENU%\Autobuild\Autobuild.lnk'); $Shortcut.TargetPath = '%INSTALL_DIR%autobuild_main.exe'; $Shortcut.Description = 'Autobuild - AI-Powered Build System'; $Shortcut.IconLocation = '%INSTALL_DIR%autobuild_main.exe,0'; $Shortcut.Save()"

REM Create Animation shortcut
powershell -Command "$WshShell = New-Object -comObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%START_MENU%\Autobuild\Autobuild Animation.lnk'); $Shortcut.TargetPath = '%INSTALL_DIR%autobuild_gui.exe'; $Shortcut.Description = 'Autobuild Animation - OpenGL 3D Animation'; $Shortcut.IconLocation = '%INSTALL_DIR%autobuild_gui.exe,0'; $Shortcut.Save()"

echo Shortcuts created successfully!
echo - Desktop: %DESKTOP%\Autobuild.lnk
echo - Start Menu: %START_MENU%\Autobuild\Autobuild.lnk
echo - Animation: %START_MENU%\Autobuild\Autobuild Animation.lnk
pause
