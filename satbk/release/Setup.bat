@echo off
setlocal EnableExtensions
title Sonic and the Black Knight PC - Setup
cd /d "%~dp0"

echo ============================================================
echo  Sonic and the Black Knight - PC port setup
echo ============================================================
echo.
echo This port contains NO game data. It extracts the files it needs
echo from YOUR OWN disc image of Sonic and the Black Knight (USA, RENE8P)
echo into the GameData folder next to SonicBlackKnight.exe.
echo Supported images: ISO, WBFS, RVZ, WIA, CISO, GCZ.
echo.

set "IMAGE=%~1"
if not "%IMAGE%"=="" goto have_image

for /f "usebackq delims=" %%I in (`powershell -NoProfile -STA -Command "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = 'Select your Sonic and the Black Knight (USA) disc image'; $d.Filter = 'Wii disc images (*.iso;*.wbfs;*.rvz;*.wia;*.ciso;*.gcz)|*.iso;*.wbfs;*.rvz;*.wia;*.ciso;*.gcz|All files (*.*)|*.*'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }"`) do set "IMAGE=%%I"

if "%IMAGE%"=="" (
    echo No disc image selected. You can also drag your disc image onto Setup.bat.
    pause
    exit /b 1
)

:have_image
echo Disc image: "%IMAGE%"
echo Extracting to "%~dp0GameData" (about 3.5 GB) ...
echo.
"%~dp0tools\satbk_extract.exe" "%IMAGE%" "%~dp0GameData"
if errorlevel 1 (
    echo.
    echo Extraction FAILED. Make sure this is a clean Sonic and the Black Knight USA disc image.
    pause
    exit /b 1
)
echo.
echo Done. Start the game with SonicBlackKnight.exe
pause
exit /b 0
