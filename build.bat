@echo off
echo compiling version.dll proxy hook...
C:\msys64\mingw64\bin\g++.exe -O2 -shared -o version.dll dllmain.cpp forwarders.s version.def -static -static-libgcc -static-libstdc++
if %ERRORLEVEL% equ 0 (
    echo compilation successful! created version.dll
) else (
    echo compilation failed with error code %ERRORLEVEL%
)
pause
