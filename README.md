# DLDT-RPC

Lightweight Discord RPC for Dying Light Developer Tools.
Shows active map/file details on discord profile.

## build
to compile, run the build script or use g++:
```cmd
g++ -O2 -shared -o version.dll dllmain.cpp forwarders.s version.def -static-libgcc -static-libstdc++
```

## install
1. copy `version.dll` (dont forget to backup the original file) to your dying light developer tools folder next to `DyingLightEditor.exe`.
2. run the developer tools. it will load the proxy and update discord.

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.