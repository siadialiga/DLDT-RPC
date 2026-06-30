#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <algorithm>
#include <vector>
#include <tlhelp32.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace chromed
{
    struct NativePluginRuntime
    {
        void *object;
        uintptr_t editor;
        uintptr_t pluginInterface;
        uintptr_t aux;
        uintptr_t pluginService;
        uint32_t pluginCode;
        HMODULE module;
        bool initialized;
        bool commandsRegistered;
        std::wstring name;
    };

    namespace
    {
        std::wofstream g_logStream;
        std::mutex g_logMutex;

        std::wstring ModuleDirectoryForLog(HMODULE module)
        {
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(module, path, MAX_PATH);
            wchar_t *slash = wcsrchr(path, L'\\');
            if (slash)
                *slash = 0;
            return path;
        }

        std::wstring TimeStamp()
        {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            std::wstringstream s;
            s << std::setfill(L'0') << std::setw(4) << st.wYear << L"-" << std::setw(2) << st.wMonth << L"-" << std::setw(2) << st.wDay << L" " << std::setw(2) << st.wHour << L":" << std::setw(2) << st.wMinute << L":" << std::setw(2) << st.wSecond;
            return s.str();
        }

        void WriteLog(const wchar_t *level, const std::wstring &message)
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            std::wstring line = TimeStamp() + L" [" + level + L"] " + message + L"\n";
            if (g_logStream.is_open())
            {
                g_logStream << line;
                g_logStream.flush();
            }
            OutputDebugStringW(line.c_str());
        }
    }

    class Log
    {
    public:
        static void Open(HMODULE module, const wchar_t *name)
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            if (g_logStream.is_open())
                return;
            std::wstring path = ModuleDirectoryForLog(module) + L"\\" + name + L".log";
            g_logStream.open(path, std::ios::out | std::ios::app);
        }

        static void Info(const std::wstring &message)
        {
            WriteLog(L"INFO", message);
        }

        static void Warn(const std::wstring &message)
        {
            WriteLog(L"WARN", message);
        }

        static void Error(const std::wstring &message)
        {
            WriteLog(L"ERROR", message);
        }

        static void Close()
        {
            std::lock_guard<std::mutex> lock(g_logMutex);
            if (g_logStream.is_open())
                g_logStream.close();
        }
    };

    std::wstring Utf8ToWide(const std::string &value)
    {
        if (value.empty())
            return L"";
        int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
        if (length <= 0)
            return L"";
        std::wstring out(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), length);
        return out;
    }

    std::string WideToUtf8(const std::wstring &value)
    {
        if (value.empty())
            return "";
        int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            return "";
        std::string out(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), length, nullptr, nullptr);
        return out;
    }

    void InitializeNativePluginObject(void *object, size_t objectSize, void **vtable, uintptr_t editor, uintptr_t pluginInterface, uintptr_t pluginService, uint32_t pluginCode)
    {
        if (!object || objectSize < 0x100)
            return;
        std::memset(object, 0, objectSize);
        *reinterpret_cast<void ***>(object) = vtable;
        *reinterpret_cast<uintptr_t *>(static_cast<uint8_t *>(object) + 0x48) = editor;
        *reinterpret_cast<uintptr_t *>(static_cast<uint8_t *>(object) + 0x50) = pluginInterface;
        *reinterpret_cast<uintptr_t *>(static_cast<uint8_t *>(object) + 0x58) = pluginService;
        *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(object) + 0x60) = pluginCode;
    }
}

namespace
{
    using namespace chromed;

    HMODULE g_module = nullptr;
    NativePluginRuntime g_runtime{};
    void *g_vtable[64] = {};
    alignas(16) uint8_t g_pluginObject[0x1000] = {};
    HANDLE g_thread = nullptr;
    HANDLE g_stopEvent = nullptr;
    HANDLE g_pipe = INVALID_HANDLE_VALUE;
    volatile LONG g_started = 0;
    volatile LONG g_running = 0;
    long long g_startTime = 0;
    volatile LONG g_nonce = 1;
    ULONGLONG g_lastCompileTick = 0;
    std::wstring g_cachedTarget;

#pragma pack(push, 1)
    struct DiscordPacket
    {
        uint32_t opcode;
        uint32_t length;
    };
#pragma pack(pop)

    struct Config
    {
        bool enabled = true;
        DWORD pollMilliseconds = 5000;
        std::string clientId = "1518223979880382618";
        std::string largeImage = "mainimage";
        std::string largeText = "Dying Light Developer Tools";
        std::string smallImage = "movinggears";
        std::string smallText = "In Editor";
        std::string stateEditing = "Editing";
        std::string stateIdle = "Selecting Map";
        std::string stateCompiling = "Compiling...";
        std::string stateInGame = "In Game";
        std::string waitingDetails = "Initializing ChromEd...";
        std::string mainMenuDetails = "Main Menu";
        bool fileNameOnly = true;
        bool detectCompiler = true;
        bool detectPlayer = true;
        std::wstring compilerProcesses = L"ResPackCompilerConsole_x64_rwdi.exe;MEConv_x64_rwdi.exe;QuestCompilerConsole_x64_rwdi.exe;ResPackCompilerConsole.exe";
        std::wstring playerProcesses = L"DyingLightPlayer.exe";
    };

    Config g_config{};

    intptr_t __fastcall Stub0()
    {
        return 0;
    }

    intptr_t __fastcall Stub1(void *)
    {
        return 0;
    }

    intptr_t __fastcall Stub2(void *, uintptr_t)
    {
        return 0;
    }

    intptr_t __fastcall Stub3(void *, uintptr_t, uintptr_t)
    {
        return 0;
    }

    intptr_t __fastcall Stub4(void *, uintptr_t, uintptr_t, uintptr_t, uintptr_t)
    {
        return 0;
    }

    std::wstring ModuleDirectory()
    {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(g_module, path, MAX_PATH);
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash)
            *slash = 0;
        return path;
    }

    std::wstring IniPath()
    {
        return ModuleDirectory() + L"\\DiscordPresence.ini";
    }

    std::wstring Lower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
                       { return static_cast<wchar_t>(towlower(ch)); });
        return value;
    }

    bool StartsWith(const std::wstring &value, const wchar_t *needle)
    {
        size_t n = wcslen(needle);
        return value.size() >= n && value.compare(0, n, needle) == 0;
    }

    bool Contains(const std::wstring &value, const wchar_t *needle)
    {
        return value.find(needle) != std::wstring::npos;
    }

    std::vector<std::wstring> SplitProcessList(const std::wstring &value)
    {
        std::vector<std::wstring> items;
        size_t start = 0;
        while (start <= value.size())
        {
            size_t end = value.find_first_of(L";,", start);
            std::wstring item = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            while (!item.empty() && iswspace(item.front()))
                item.erase(item.begin());
            while (!item.empty() && iswspace(item.back()))
                item.pop_back();
            if (!item.empty())
                items.push_back(Lower(item));
            if (end == std::wstring::npos)
                break;
            start = end + 1;
        }
        return items;
    }

    bool IsAnyProcessRunning(const std::wstring &processList)
    {
        std::vector<std::wstring> names = SplitProcessList(processList);
        if (names.empty())
            return false;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                std::wstring exe = Lower(entry.szExeFile);
                for (const std::wstring &name : names)
                {
                    if (exe == name)
                    {
                        found = true;
                        break;
                    }
                }
            } while (!found && Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    bool TryWindowText(HWND hwnd, std::wstring &out, DWORD timeoutMs)
    {
        wchar_t buffer[512]{};
        DWORD_PTR result = 0;
        LRESULT ok = SendMessageTimeoutW(hwnd, WM_GETTEXT, static_cast<WPARAM>(std::size(buffer)), reinterpret_cast<LPARAM>(buffer), SMTO_ABORTIFHUNG | SMTO_BLOCK, timeoutMs, &result);
        if (!ok)
            return false;
        out = buffer;
        return true;
    }

    bool TryWindowTitle(HWND hwnd, std::wstring &out)
    {
        return TryWindowText(hwnd, out, 50);
    }

    struct CompileWindowSearch
    {
        bool found;
    };

    BOOL CALLBACK EnumCompileWindows(HWND hwnd, LPARAM lparam)
    {
        CompileWindowSearch *search = reinterpret_cast<CompileWindowSearch *>(lparam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd))
            return TRUE;
        std::wstring title;
        if (!TryWindowTitle(hwnd, title))
            return TRUE;
        std::wstring lowerTitle = Lower(title);
        if (Contains(lowerTitle, L"compile map") || Contains(lowerTitle, L"compiling map"))
        {
            search->found = true;
            return FALSE;
        }
        return TRUE;
    }

    bool IsCompileMapWindowVisible()
    {
        CompileWindowSearch search{false};
        EnumWindows(EnumCompileWindows, reinterpret_cast<LPARAM>(&search));
        if (search.found)
            g_lastCompileTick = GetTickCount64();
        if (!search.found && g_lastCompileTick != 0 && GetTickCount64() - g_lastCompileTick < 15000)
            return true;
        return search.found;
    }

    std::wstring ReadIniWide(const wchar_t *name, const wchar_t *fallback)
    {
        wchar_t buffer[512]{};
        GetPrivateProfileStringW(L"DiscordRpc", name, fallback, buffer, static_cast<DWORD>(std::size(buffer)), IniPath().c_str());
        return buffer;
    }

    DWORD ReadIniDword(const wchar_t *name, DWORD fallback)
    {
        return GetPrivateProfileIntW(L"DiscordRpc", name, static_cast<INT>(fallback), IniPath().c_str());
    }

    bool ReadIniBool(const wchar_t *name, bool fallback)
    {
        return ReadIniDword(name, fallback ? 1 : 0) != 0;
    }
    // make nice ini
    void WriteDefaultIni()
    {
        std::wstring path = IniPath();
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            return;
        WritePrivateProfileStringW(L"DiscordRpc", L"Enabled", L"1", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"ClientId", L"1518223979880382618", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"LargeImage", L"mainimage", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"LargeText", L"Dying Light Developer Tools", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"SmallImage", L"movinggears", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"SmallText", L"In Editor", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"StateEditing", L"Editing", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"StateIdle", L"Selecting Map", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"StateCompiling", L"Compiling...", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"StateInGame", L"In Game", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"WaitingDetails", L"Initializing ChromEd...", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"MainMenuDetails", L"Main Menu", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"PollMilliseconds", L"5000", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"FileNameOnly", L"1", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"DetectCompiler", L"1", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"DetectPlayer", L"1", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"CompilerProcesses", L"ResPackCompilerConsole_x64_rwdi.exe;MEConv_x64_rwdi.exe;QuestCompilerConsole_x64_rwdi.exe;ResPackCompilerConsole.exe", path.c_str());
        WritePrivateProfileStringW(L"DiscordRpc", L"PlayerProcesses", L"DyingLightPlayer.exe", path.c_str());
    }
    // load that nice ini
    void LoadConfig()
    {
        WriteDefaultIni();
        Config next{};
        next.enabled = ReadIniBool(L"Enabled", true);
        next.pollMilliseconds = ReadIniDword(L"PollMilliseconds", 5000);
        if (next.pollMilliseconds < 1000)
            next.pollMilliseconds = 1000;
        next.clientId = WideToUtf8(ReadIniWide(L"ClientId", L"1518223979880382618"));
        next.largeImage = WideToUtf8(ReadIniWide(L"LargeImage", L"mainimage"));
        next.largeText = WideToUtf8(ReadIniWide(L"LargeText", L"Dying Light Developer Tools"));
        next.smallImage = WideToUtf8(ReadIniWide(L"SmallImage", L"movinggears"));
        next.smallText = WideToUtf8(ReadIniWide(L"SmallText", L"In Editor"));
        next.stateEditing = WideToUtf8(ReadIniWide(L"StateEditing", L"Editing"));
        next.stateIdle = WideToUtf8(ReadIniWide(L"StateIdle", L"Selecting Map"));
        next.stateCompiling = WideToUtf8(ReadIniWide(L"StateCompiling", L"Compiling..."));
        next.stateInGame = WideToUtf8(ReadIniWide(L"StateInGame", L"In Game"));
        next.waitingDetails = WideToUtf8(ReadIniWide(L"WaitingDetails", L"Initializing ChromEd..."));
        next.mainMenuDetails = WideToUtf8(ReadIniWide(L"MainMenuDetails", L"Main Menu"));
        next.fileNameOnly = ReadIniBool(L"FileNameOnly", true);
        next.detectCompiler = ReadIniBool(L"DetectCompiler", true);
        next.detectPlayer = ReadIniBool(L"DetectPlayer", true);
        next.compilerProcesses = ReadIniWide(L"CompilerProcesses", L"ResPackCompilerConsole_x64_rwdi.exe;MEConv_x64_rwdi.exe;QuestCompilerConsole_x64_rwdi.exe;ResPackCompilerConsole.exe");
        next.playerProcesses = ReadIniWide(L"PlayerProcesses", L"DyingLightPlayer.exe");
        g_config = next;
        chromed::Log::Info(L"ChromeDiscordRpc config loaded from " + IniPath());
    }

    std::wstring WindowClass(HWND hwnd)
    {
        wchar_t buffer[256]{};
        GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
        return Lower(buffer);
    }

    std::wstring WindowTitle(HWND hwnd)
    {
        std::wstring title;
        if (!TryWindowTitle(hwnd, title))
            return L"";
        return title;
    }

    struct WindowInfo
    {
        DWORD pid;
        std::wstring title;
    };

    BOOL CALLBACK EnumEditorWindows(HWND hwnd, LPARAM lparam)
    {
        WindowInfo *info = reinterpret_cast<WindowInfo *>(lparam);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != info->pid || GetParent(hwnd) != nullptr || !IsWindowVisible(hwnd))
            return TRUE;
        std::wstring title = WindowTitle(hwnd);
        std::wstring lowerTitle = Lower(title);
        std::wstring cls = WindowClass(hwnd);
        if ((StartsWith(cls, L"afx") || Contains(cls, L"wnd")) && Contains(lowerTitle, L"dying light developer tools"))
        {
            info->title = title;
            if (Contains(lowerTitle, L" - dying light developer tools"))
                return FALSE;
        }
        return TRUE;
    }

    std::wstring FileNameFromPath(const std::wstring &path)
    {
        size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            return path.substr(slash + 1);
        return path;
    }

    std::wstring CurrentEditorTarget()
    {
        WindowInfo info{GetCurrentProcessId(), L""};
        EnumWindows(EnumEditorWindows, reinterpret_cast<LPARAM>(&info));
        if (info.title.empty())
        {
            if (!g_cachedTarget.empty())
                return g_cachedTarget;
            return Utf8ToWide(g_config.waitingDetails);
        }
        std::wstring marker = L" - Dying Light Developer Tools";
        size_t pos = info.title.find(marker);
        if (pos == std::wstring::npos)
        {
            if (!g_cachedTarget.empty())
                return g_cachedTarget;
            return Utf8ToWide(g_config.mainMenuDetails);
        }
        std::wstring path = info.title.substr(0, pos);
        if (path.empty())
        {
            if (!g_cachedTarget.empty())
                return g_cachedTarget;
            return Utf8ToWide(g_config.mainMenuDetails);
        }
        std::wstring result = g_config.fileNameOnly ? FileNameFromPath(path) : path;
        g_cachedTarget = result;
        return result;
    }

    std::string EscapeJson(const std::string &value)
    {
        std::string out;
        out.reserve(value.size() + 8);
        for (char ch : value)
        {
            switch (ch)
            {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20)
                    out += ' ';
                else
                    out += ch;
                break;
            }
        }
        return out;
    }

    void ClosePipe()
    {
        if (g_pipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_pipe);
            g_pipe = INVALID_HANDLE_VALUE;
        }
    }

    bool WritePacket(uint32_t opcode, const std::string &payload)
    {
        if (g_pipe == INVALID_HANDLE_VALUE)
            return false;
        DiscordPacket packet{opcode, static_cast<uint32_t>(payload.size())};
        DWORD written = 0;
        if (!WriteFile(g_pipe, &packet, sizeof(packet), &written, nullptr) || written != sizeof(packet))
            return false;
        if (!payload.empty() && (!WriteFile(g_pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr) || written != static_cast<DWORD>(payload.size())))
            return false;
        return true;
    }

    bool ConnectDiscord()
    {
        if (g_pipe != INVALID_HANDLE_VALUE)
            return true;
        for (int i = 0; i < 10; ++i)
        {
            char pipeName[64]{};
            sprintf_s(pipeName, "\\\\.\\pipe\\discord-ipc-%d", i);
            g_pipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (g_pipe != INVALID_HANDLE_VALUE)
            {
                chromed::Log::Info(L"ChromeDiscordRpc connected to Discord pipe " + std::to_wstring(i));
                break;
            }
        }
        if (g_pipe == INVALID_HANDLE_VALUE)
            return false;
        std::string handshake = "{\"v\":1,\"client_id\":\"" + EscapeJson(g_config.clientId) + "\"}";
        if (!WritePacket(0, handshake))
        {
            chromed::Log::Warn(L"ChromeDiscordRpc handshake write failed");
            ClosePipe();
            return false;
        }
        return true;
    }

    std::string NextNonce()
    {
        LONG nonce = InterlockedIncrement(&g_nonce);
        return std::to_string(GetCurrentProcessId()) + "-" + std::to_string(static_cast<long long>(nonce));
    }

    bool SendActivity(const std::string &details, const std::string &state, const std::string &smallImage, const std::string &smallText)
    {
        if (!g_config.enabled || !ConnectDiscord())
            return false;
        std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"activity\":{\"state\":\"" + EscapeJson(state) + "\",\"details\":\"" + EscapeJson(details) + "\",\"timestamps\":{\"start\":" + std::to_string(g_startTime) + "},\"assets\":{\"large_image\":\"" + EscapeJson(g_config.largeImage) + "\",\"large_text\":\"" + EscapeJson(g_config.largeText) + "\",\"small_image\":\"" + EscapeJson(smallImage) + "\",\"small_text\":\"" + EscapeJson(smallText) + "\"}}},\"nonce\":\"" + NextNonce() + "\"}";
        if (!WritePacket(1, payload))
        {
            chromed::Log::Warn(L"ChromeDiscordRpc activity write failed");
            ClosePipe();
            return false;
        }
        chromed::Log::Info(L"ChromeDiscordRpc activity updated: " + Utf8ToWide(details) + L" / " + Utf8ToWide(state));
        return true;
    }
    // bye bye
    void ClearActivity()
    {
        if (!g_config.enabled || !ConnectDiscord())
            return;
        std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) + ",\"activity\":null},\"nonce\":\"" + NextNonce() + "\"}";
        WritePacket(1, payload);
    }

    struct ActivitySnapshot
    {
        std::string details;
        std::string state;
        std::string smallImage;
        std::string smallText;
    };

    ActivitySnapshot BuildActivitySnapshot()
    {
        std::wstring target = CurrentEditorTarget();
        std::string details = WideToUtf8(target);
        bool idle = details == g_config.waitingDetails || details == g_config.mainMenuDetails;
        std::string state = idle ? g_config.stateIdle : g_config.stateEditing;
        std::string smallImage = g_config.smallImage;
        std::string smallText = g_config.smallText;
        bool compilerRunning = g_config.detectCompiler && IsAnyProcessRunning(g_config.compilerProcesses);
        bool compileWindowVisible = g_config.detectCompiler && IsCompileMapWindowVisible();
        if (compilerRunning)
            g_lastCompileTick = GetTickCount64();
        bool compileHold = g_lastCompileTick != 0 && GetTickCount64() - g_lastCompileTick < 15000;
        if (g_config.detectCompiler && (compileWindowVisible || compilerRunning || compileHold))
        {
            state = g_config.stateCompiling;
            smallImage = "movinggears";
            smallText = "Compiling...";
        }
        else if (g_config.detectPlayer && IsAnyProcessRunning(g_config.playerProcesses))
        {
            state = g_config.stateInGame;
            smallImage = "playing";
            smallText = "In Game";
        }
        return ActivitySnapshot{details, state, smallImage, smallText};
    }

    void UpdateNow()
    {
        ActivitySnapshot activity = BuildActivitySnapshot();
        SendActivity(activity.details, activity.state, activity.smallImage, activity.smallText);
    }

    DWORD WINAPI RpcThread(void *)
    {
        chromed::Log::Info(L"ChromeDiscordRpc thread started");
        g_startTime = static_cast<long long>(time(nullptr));
        std::string lastDetails;
        std::string lastState;
        std::string lastSmallImage;
        std::string lastSmallText;
        DWORD configTick = 0;
        while (InterlockedCompareExchange(&g_running, 1, 1) == 1)
        {
            if ((configTick++ % 12) == 0)
                LoadConfig();
            ActivitySnapshot activity = BuildActivitySnapshot();
            if (g_config.enabled && (activity.details != lastDetails || activity.state != lastState || activity.smallImage != lastSmallImage || activity.smallText != lastSmallText || g_pipe == INVALID_HANDLE_VALUE))
            {
                if (SendActivity(activity.details, activity.state, activity.smallImage, activity.smallText))
                {
                    lastDetails = activity.details;
                    lastState = activity.state;
                    lastSmallImage = activity.smallImage;
                    lastSmallText = activity.smallText;
                }
            }
            DWORD waitMs = g_config.pollMilliseconds;
            if (waitMs < 250)
                waitMs = 250;
            if (waitMs > 1000)
                waitMs = 1000;
            if (WaitForSingleObject(g_stopEvent, waitMs) == WAIT_OBJECT_0)
                break;
        }
        ClearActivity();
        ClosePipe();
        chromed::Log::Info(L"ChromeDiscordRpc thread stopped");
        return 0;
    }

    void StartRpc()
    {
        if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
            return;
        LoadConfig();
        if (!g_config.enabled)
        {
            chromed::Log::Info(L"ChromeDiscordRpc is disabled by INI");
            return;
        }
        g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!g_stopEvent)
        {
            chromed::Log::Error(L"ChromeDiscordRpc failed to create stop event");
            return;
        }
        InterlockedExchange(&g_running, 1);
        g_thread = CreateThread(nullptr, 0, RpcThread, nullptr, 0, nullptr);
        if (!g_thread)
        {
            chromed::Log::Error(L"ChromeDiscordRpc failed to create thread");
            InterlockedExchange(&g_running, 0);
            SetEvent(g_stopEvent);
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
    }

    void StopRpc(bool wait)
    {
        InterlockedExchange(&g_running, 0);
        if (g_stopEvent)
            SetEvent(g_stopEvent);
        if (!wait)
            return;
        if (g_thread)
            WaitForSingleObject(g_thread, 2000);
        if (g_thread)
        {
            CloseHandle(g_thread);
            g_thread = nullptr;
        }
        if (g_stopEvent)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = nullptr;
        }
        ClosePipe();
    }

    intptr_t __fastcall PluginLoad(void *)
    {
        chromed::Log::Info(L"ChromeDiscordRpc plugin load callback");
        StartRpc();
        return 1;
    }

    intptr_t __fastcall PluginCode(void *)
    {
        return g_runtime.pluginCode;
    }

    void BuildVtable()
    {
        for (void *&entry : g_vtable)
            entry = reinterpret_cast<void *>(&Stub4);
        g_vtable[0] = reinterpret_cast<void *>(&Stub2);
        g_vtable[1] = reinterpret_cast<void *>(&Stub1);
        g_vtable[2] = reinterpret_cast<void *>(&Stub1);
        g_vtable[3] = reinterpret_cast<void *>(&Stub0);
        g_vtable[4] = reinterpret_cast<void *>(&Stub0);
        g_vtable[5] = reinterpret_cast<void *>(&Stub0);
        g_vtable[6] = reinterpret_cast<void *>(&Stub0);
        g_vtable[8] = reinterpret_cast<void *>(&PluginLoad);
        g_vtable[12] = reinterpret_cast<void *>(&Stub3);
        g_vtable[16] = reinterpret_cast<void *>(&PluginCode);
    }
}

extern "C" __declspec(dllexport) void ChromeDiscordRpc_UpdateNow()
{
    LoadConfig();
    UpdateNow();
}

extern "C" __declspec(dllexport) void ChromeDiscordRpc_StopNow()
{
    StopRpc(true);
}

extern "C" __declspec(dllexport) void *__fastcall InitializeMe(uintptr_t editor, uint32_t pluginIndex, uintptr_t pluginInterface, uintptr_t aux, int64_t)
{
    uint32_t pluginCode = (pluginIndex & 0x3F | 0x40) << 8;
    chromed::InitializeNativePluginObject(g_pluginObject, sizeof(g_pluginObject), g_vtable, editor, pluginInterface, 0, pluginCode);
    g_runtime.object = g_pluginObject;
    g_runtime.editor = editor;
    g_runtime.pluginInterface = pluginInterface;
    g_runtime.pluginService = 0;
    g_runtime.aux = aux;
    g_runtime.pluginCode = pluginCode;
    g_runtime.module = g_module;
    g_runtime.name = L"ChromeDiscordRpc";
    chromed::Log::Info(L"ChromeDiscordRpc InitializeMe editor=0x" + std::to_wstring(editor) + L" pluginIndex=" + std::to_wstring(pluginIndex));
    return g_pluginObject;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        BuildVtable();
        chromed::Log::Open(module, L"ChromeDiscordRpc");
        chromed::Log::Info(L"ChromeDiscordRpc DLL_PROCESS_ATTACH");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        StopRpc(false);
        chromed::Log::Info(L"ChromeDiscordRpc DLL_PROCESS_DETACH");
        chromed::Log::Close();
    }
    return TRUE;
}
