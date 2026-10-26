// launcher/main.cpp — minimal fail-closed launcher for zzz-touch-unlocker.
#include <windows.h>

#include "inject.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kDefaultGamePath[] =
    L"C:\\Program Files\\miHoYo Launcher\\games\\ZenlessZoneZero Game\\ZenlessZoneZero.exe";

struct Options {
    std::wstring game = kDefaultGamePath;
    int mode = 1;
    std::vector<std::wstring> gameArgs;
};

void PrintUsage() {
    wprintf(L"zzz-touch-unlocker.exe - runtime native touch restore\n\n");
    wprintf(L"Usage:\n");
    wprintf(L"  zzz-touch-unlocker.exe\n");
    wprintf(L"  zzz-touch-unlocker.exe --game \"C:\\path\\ZenlessZoneZero.exe\"\n\n");
    wprintf(L"  -m, --mode 0|1  Desktop (0) or mobile (1, default)\n\n");
    wprintf(L"Other arguments are forwarded to the game in order.\n");
    wprintf(L"Use -- to forward all remaining arguments literally.\n\n");
    wprintf(L"Default game:\n  %ls\n", kDefaultGamePath);
}

bool ParseArgs(int argc, wchar_t** argv, Options& out) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--") {
            while (++i < argc) out.gameArgs.emplace_back(argv[i]);
            break;
        }
        if (arg == L"-m" || arg == L"--mode") {
            if (i + 1 >= argc || (std::wstring(argv[i + 1]) != L"0" &&
                                  std::wstring(argv[i + 1]) != L"1")) {
                wprintf(L"ERROR: mode must be 0 (desktop) or 1 (mobile).\n");
                return false;
            }
            out.mode = argv[++i][0] - L'0';
            continue;
        }
        if (arg == L"--game") {
            if (i + 1 >= argc) return false;
            out.game = argv[++i];
            continue;
        }
        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            PrintUsage();
            ExitProcess(0);
        }
        out.gameArgs.push_back(arg);
    }
    return true;
}

// Windows argv quoting: double backslashes before quotes and the closing quote.
// Always quote so empty arguments and whitespace survive CreateProcessW.
std::wstring QuoteArgument(const std::wstring& arg) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        result += c;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    result += L'"';
    return result;
}

std::wstring BuildGameCommandLine(const std::wstring& gamePath,
                                const std::vector<std::wstring>& args) {
    std::wstring command = QuoteArgument(gamePath);
    for (const auto& arg : args) command += L" " + QuoteArgument(arg);
    return command;
}

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    const auto pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

std::wstring GetDllPath() {
    return GetExeDir() + L"\\zzz-touch-hook.dll";
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool InjectDll(HANDLE process, const std::wstring& dllPath) {
    return zzz::inject::InjectDllByHandle(process, dllPath);
}

bool LaunchSuspendedAndInject(const std::wstring& gamePath,
                              const std::wstring& dllPath,
                              const std::vector<std::wstring>& gameArgs) {
    std::wstring cmdline = BuildGameCommandLine(gamePath, gameArgs);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    wprintf(L"[launch] %ls\n", cmdline.c_str());
    if (!CreateProcessW(gamePath.c_str(), cmdline.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        wprintf(L"CreateProcessW failed: %lu\n", GetLastError());
        return false;
    }
    wprintf(L"[launch] pid=%lu suspended\n", pi.dwProcessId);

    auto fail = [&](const wchar_t* reason) {
        wprintf(L"[launch] %ls; terminating suspended game\n", reason);
        TerminateProcess(pi.hProcess, ERROR_DLL_INIT_FAILED);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    };

    wchar_t readyName[128]{};
    swprintf_s(readyName, L"Local\\ZZZTouchHookReady_%lu", pi.dwProcessId);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, readyName);
    if (!ready) return fail(L"ready event creation failed");

    if (!InjectDll(pi.hProcess, dllPath)) {
        CloseHandle(ready);
        return fail(L"DLL injection failed");
    }

    const DWORD wr = WaitForSingleObject(ready, 10000);
    CloseHandle(ready);
    if (wr != WAIT_OBJECT_0) return fail(L"hook bootstrap timeout/error");
    wprintf(L"[launch] bootstrap ready: yes\n");

    if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1))
        return fail(L"ResumeThread failed");

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options opt;
    if (!ParseArgs(argc, argv, opt)) {
        PrintUsage();
        return 1;
    }

    // This launcher is structurally incompatible with Wine: it creates the
    // game suspended and injects from its own process, which makes the
    // unlocker a wineserver client that loads zzz-touch-hook.dll before the
    // game's anti-cheat runs. HoYoKProtect then sees a foreign first client and
    // the game exits silently. Wine launches go through wine/zzz-touch-unlocker
    // instead (game first, attach injection second).
    if (zzz::inject::IsWine()) {
        const std::string wine = zzz::inject::WineVersion();
        wprintf(L"ERROR: zzz-touch-unlocker.exe cannot run under Wine (wine=%hs).\n",
                wine.c_str());
        wprintf(L"Wine requires the game to be the first wineserver client:\n");
        wprintf(L"  wine/zzz-touch-unlocker run -m %d\n", opt.mode);
        wprintf(L"See wine/README.md for the launch-ordering rules.\n");
        return 4;
    }

    const std::wstring dllPath = GetDllPath();
    if (!FileExists(dllPath)) {
        wprintf(L"DLL not found: %ls\n", dllPath.c_str());
        wprintf(L"Build the project first (scripts\\build.bat).\n");
        return 2;
    }
    if (!FileExists(opt.game)) {
        wprintf(L"Game executable not found: %ls\n", opt.game.c_str());
        wprintf(L"Use --game \"...\\ZenlessZoneZero.exe\" to override the default path.\n");
        return 1;
    }

    wprintf(L"zzz-touch-unlocker\n");
    wprintf(L"  game    = %ls\n", opt.game.c_str());
    wprintf(L"  locator = PATTERN_ONLY\n");
    wprintf(L"  mode    = %d (%ls)\n", opt.mode, opt.mode ? L"mobile" : L"desktop");
    // Process-local environment inherited by the suspended child and its DLL.
    if (!SetEnvironmentVariableW(L"ZZZ_TOUCH_MODE", opt.mode ? L"1" : L"0")) {
        wprintf(L"ERROR: cannot pass mode to game: %lu\n", GetLastError());
        return 3;
    }
    return LaunchSuspendedAndInject(opt.game, dllPath, opt.gameArgs) ? 0 : 3;
}
