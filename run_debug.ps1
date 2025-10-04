Write-Host "Starting Autobuild 2.0 in Debug Mode..." -ForegroundColor Green
Write-Host "This will show all debug information in the console." -ForegroundColor Yellow
Write-Host "ImGui assertions will be caught and logged instead of showing dialog boxes." -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop." -ForegroundColor Yellow
Write-Host ""
& "native\build-gui\autobuild_gui.exe" --debug --no-assert
Read-Host "Press Enter to continue"
