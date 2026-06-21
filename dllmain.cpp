#include <windows.h>
#include <iostream>
#include <thread>
#include <string>
#include <chrono>
#include <time.h>
#include <stdio.h>

// forwarders for original dll
extern "C" {
    FARPROC pGetFileVersionInfoA = NULL;
    FARPROC pGetFileVersionInfoByHandle = NULL;
    FARPROC pGetFileVersionInfoExA = NULL;
    FARPROC pGetFileVersionInfoExW = NULL;
    FARPROC pGetFileVersionInfoSizeA = NULL;
    FARPROC pGetFileVersionInfoSizeExA = NULL;
    FARPROC pGetFileVersionInfoSizeExW = NULL;
    FARPROC pGetFileVersionInfoSizeW = NULL;
    FARPROC pGetFileVersionInfoW = NULL;
    FARPROC pVerFindFileA = NULL;
    FARPROC pVerFindFileW = NULL;
    FARPROC pVerInstallFileA = NULL;
    FARPROC pVerInstallFileW = NULL;
    FARPROC pVerLanguageNameA = NULL;
    FARPROC pVerLanguageNameW = NULL;
    FARPROC pVerQueryValueA = NULL;
    FARPROC pVerQueryValueW = NULL;
}

void LoadOriginalDll() {
    wchar_t systemPath[MAX_PATH];
    GetSystemDirectoryW(systemPath, MAX_PATH);
    wcscat_s(systemPath, L"\\version.dll");
    
    HMODULE hOriginal = LoadLibraryW(systemPath);
    if (!hOriginal) { ExitProcess(0); }
    
    pGetFileVersionInfoA = GetProcAddress(hOriginal, "GetFileVersionInfoA");
    pGetFileVersionInfoByHandle = GetProcAddress(hOriginal, "GetFileVersionInfoByHandle");
    pGetFileVersionInfoExA = GetProcAddress(hOriginal, "GetFileVersionInfoExA");
    pGetFileVersionInfoExW = GetProcAddress(hOriginal, "GetFileVersionInfoExW");
    pGetFileVersionInfoSizeA = GetProcAddress(hOriginal, "GetFileVersionInfoSizeA");
    pGetFileVersionInfoSizeExA = GetProcAddress(hOriginal, "GetFileVersionInfoSizeExA");
    pGetFileVersionInfoSizeExW = GetProcAddress(hOriginal, "GetFileVersionInfoSizeExW");
    pGetFileVersionInfoSizeW = GetProcAddress(hOriginal, "GetFileVersionInfoSizeW");
    pGetFileVersionInfoW = GetProcAddress(hOriginal, "GetFileVersionInfoW");
    pVerFindFileA = GetProcAddress(hOriginal, "VerFindFileA");
    pVerFindFileW = GetProcAddress(hOriginal, "VerFindFileW");
    pVerInstallFileA = GetProcAddress(hOriginal, "VerInstallFileA");
    pVerInstallFileW = GetProcAddress(hOriginal, "VerInstallFileW");
    pVerLanguageNameA = GetProcAddress(hOriginal, "VerLanguageNameA");
    pVerLanguageNameW = GetProcAddress(hOriginal, "VerLanguageNameW");
    pVerQueryValueA = GetProcAddress(hOriginal, "VerQueryValueA");
    pVerQueryValueW = GetProcAddress(hOriginal, "VerQueryValueW");
}

// discord rpc implementation

#pragma pack(push, 1)
struct DiscordPacket {
    uint32_t opcode; // 0 handshake, 1 frame, 2 close
    uint32_t length;
};
#pragma pack(pop)

const char* DISCORD_CLIENT_ID = "1518223979880382618"; 

bool g_discordRunning = true;
HANDLE g_pipe = INVALID_HANDLE_VALUE;
long long g_startTime = 0;

std::string EscapeJSON(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

bool ConnectToDiscord() {
    if (g_pipe != INVALID_HANDLE_VALUE) return true;
    
    char pipeName[64];
    for (int i = 0; i < 10; ++i) {
        snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);
        g_pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (g_pipe != INVALID_HANDLE_VALUE) {
            break;
        }
    }
    
    if (g_pipe == INVALID_HANDLE_VALUE) return false;

    // handshake
    char handshake[256];
    snprintf(handshake, sizeof(handshake), "{\"v\": 1, \"client_id\": \"%s\"}", DISCORD_CLIENT_ID);
    
    DiscordPacket packet = {0, (uint32_t)strlen(handshake)};
    DWORD written = 0;
    WriteFile(g_pipe, &packet, sizeof(packet), &written, NULL);
    WriteFile(g_pipe, handshake, packet.length, &written, NULL);
    Sleep(100);
    return true;
}

void UpdateActivity(const std::string& mapName) {
    if (!ConnectToDiscord()) return;

    DWORD pid = GetCurrentProcessId();
    std::string safeMapName = EscapeJSON(mapName);
    
    std::string state = "Editing";
    if (mapName == "Main Menu" || mapName == "Waiting...") {
        state = "Selecting";
    }
    
    char payload[2048];
    snprintf(payload, sizeof(payload), 
        "{"
        "\"cmd\": \"SET_ACTIVITY\","
        "\"args\": {"
            "\"pid\": %lu,"
            "\"activity\": {"
                "\"state\": \"%s\","
                "\"details\": \"%s\","
                "\"timestamps\": {"
                    "\"start\": %lld"
                "},"
                "\"assets\": {"
                    "\"large_image\": \"default_icon\","
                    "\"large_text\": \"Dying Light Developer Tools\""
                "}"
            "}"
        "},"
        "\"nonce\": \"1\""
        "}",
        pid, state.c_str(), safeMapName.c_str(), g_startTime
    );

    DiscordPacket packet = {1, (uint32_t)strlen(payload)};
    DWORD written = 0;
    if (!WriteFile(g_pipe, &packet, sizeof(packet), &written, NULL) ||
        !WriteFile(g_pipe, payload, packet.length, &written, NULL)) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

// window title extract

struct WindowInfo {
    DWORD pid;
    std::string title;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowInfo* info = (WindowInfo*)lParam;
    DWORD procId = 0;
    GetWindowThreadProcessId(hwnd, &procId);
    
    if (procId == info->pid) {
        if (GetParent(hwnd) == NULL && IsWindowVisible(hwnd)) {
            char title[512] = {0};
            char className[256] = {0};
            GetClassNameA(hwnd, className, sizeof(className));
            GetWindowTextA(hwnd, title, sizeof(title));
            
            std::string sTitle(title);
            std::string sClass(className);
            
            // afx window check
            if (sClass.find("Afx") == 0 && sTitle.find("Dying Light Developer Tools") != std::string::npos) {
                // map title check
                if (sTitle.find(" - Dying Light Developer Tools") != std::string::npos) {
                    info->title = sTitle;
                    return FALSE; 
                }
                // fallback
                info->title = sTitle;
            }
        }
    }
    return TRUE;
}

std::string GetCurrentMapName() {
    WindowInfo info = {GetCurrentProcessId(), ""};
    EnumWindows(EnumWindowsProc, (LPARAM)&info);
    
    if (info.title.empty()) return "Waiting...";
    
    size_t pos = info.title.find(" - Dying Light Developer Tools");
    if (pos != std::string::npos) {
        std::string mapPath = info.title.substr(0, pos);
        
        // get file name from path
        size_t slashPos = mapPath.find_last_of("/\\");
        if (slashPos != std::string::npos) {
            return mapPath.substr(slashPos + 1);
        }
        return mapPath;
    }
    
    return "Main Menu";
}

void DiscordThread() {
    g_startTime = time(NULL);
    std::string lastMapName = "";

    while (g_discordRunning) {
        std::string currentMapName = GetCurrentMapName();
        if (currentMapName != lastMapName || g_pipe == INVALID_HANDLE_VALUE) {
            UpdateActivity(currentMapName);
            lastMapName = currentMapName;
        }
        
        // check every 5 seconds
        for (int i = 0; i < 50; ++i) {
            if (!g_discordRunning) break;
            Sleep(100);
        }
    }
    
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
    }
}

// dll entryyyy

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            LoadOriginalDll();
            std::thread(DiscordThread).detach();
            break;
        case DLL_PROCESS_DETACH:
            g_discordRunning = false;
            Sleep(200);
            break;
    }
    return TRUE;
}
