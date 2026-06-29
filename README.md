# DLDT-RPC

Lightweight Discord RPC for Dying Light Developer Tools.
Shows active map/file details on discord profile.

## build
To compile, run the build script or use g++:
```cmd
g++ -O2 -shared -o DiscordPresence.dll src\dllmain.cpp -static-libgcc -static-libstdc++
```

## install
1. Copy `DiscordPresence.dll` to your `Dying Light Developer Tools\Plugins_x64_rwdi` directory.
2. Run the developer tools. it will load the plugin automatically.

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.