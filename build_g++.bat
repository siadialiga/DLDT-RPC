@echo off
echo Compiling version.dll proxy hook...
C:\msys64\mingw64\bin\g++.exe -O2 -shared -o DiscordPresence.dll src\dllmain.cpp -static -static-libgcc -static-libstdc++
if %ERRORLEVEL% equ 0 (
    echo Compilation successful! created DiscordPresence.dll
) else (
    echo Compilation failed with error code %ERRORLEVEL%
)
pause
