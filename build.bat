@echo off
echo Compiling version.dll proxy hook...
C:\msys64\mingw64\bin\g++.exe -O2 -shared -o version.dll src\dllmain.cpp src\forwarders.s src\version.def -static -static-libgcc -static-libstdc++
if %ERRORLEVEL% equ 0 (
    echo Compilation successful! created version.dll
) else (
    echo Compilation failed with error code %ERRORLEVEL%
)
pause
