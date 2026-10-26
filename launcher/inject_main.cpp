// inject_main.cpp — zzz-touch-inject.exe
//
// Attach-mode injector. The Windows launcher creates the game suspended and
// injects before the first instruction runs. That ordering is impossible under
// Wine (see wine/README.md): every launcher-side wineserver client loads
// zzz-touch-hook.dll first, which makes HoYoKProtect see a foreign client
// before the game and terminate it silently after ~5 s.
//
// This tool is therefore started *after* the game process exists, as a second
// wineserver client, and injects into the already running game.
//
// Usage:
//   zzz-touch-inject.exe [--name NAME] [--pid N] [--dll PATH] [--mode 0|1]
//                        [--wait MS] [--timeout MS] [--quiet]
//                        [--no-inject] [--help]
#include "inject.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct Options {
    std::wstring name = L"ZenlessZoneZero.exe";
    std::wstring dll;
    DWORD pid = 0;
    DWORD wait_ms = 0;      // 0 = look up immediately
    DWORD timeout_ms = zzz::inject::kDefaultInjectTimeoutMs;
    int mode = -1;          // -1 = do not touch the mode file
    bool inject = true;
    bool quiet = false;
    bool help = false;
};

void Usage() {
    wprintf(
        L"zzz-touch-inject — attach-mode injector for zzz-touch-hook.dll\n"
        L"\n"
        L"  --name NAME      target image basename (default ZenlessZoneZero.exe)\n"
        L"  --pid N          target pid (overrides --name lookup)\n"
        L"  --dll PATH       hook DLL path (default: <exe dir>\\zzz-touch-hook.dll)\n"
        L"  --mode 0|1       write zzz-touch-mode.txt next to the DLL before\n"
        L"                   injecting (0 = DESKTOP, 1 = MOBILE)\n"
        L"  --wait MS        wait up to MS for the target process (default 0)\n"
        L"  --timeout MS     LoadLibraryW completion timeout (default %lu)\n"
        L"  --no-inject      only resolve the target / write the mode file\n"
        L"  --quiet          suppress progress lines, keep errors\n"
        L"  --help           this text\n"
        L"\n"
        L"Exit codes: 0 ok, 2 usage, 3 target not found, 4 open process failed,\n"
        L"            5 injection failed, 6 mode file failed\n",
        static_cast<unsigned long>(zzz::inject::kDefaultInjectTimeoutMs));
}

bool ParseInt(const wchar_t* text, int* out) {
    if (!text || !*text) return false;
    wchar_t* end = nullptr;
    const long value = wcstol(text, &end, 10);
    if (end == text || (end && *end)) return false;
    *out = static_cast<int>(value);
    return true;
}

bool ParseArgs(int argc, wchar_t** argv, Options* opt) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&](const wchar_t* what) -> const wchar_t* {
            if (i + 1 >= argc) {
                fwprintf(stderr, L"missing value for %ls\n", what);
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == L"--name") {
            const wchar_t* v = next(L"--name");
            if (!v) return false;
            opt->name = v;
        } else if (arg == L"--pid") {
            const wchar_t* v = next(L"--pid");
            int value = 0;
            if (!v || !ParseInt(v, &value) || value <= 0) return false;
            opt->pid = static_cast<DWORD>(value);
        } else if (arg == L"--dll") {
            const wchar_t* v = next(L"--dll");
            if (!v) return false;
            opt->dll = v;
        } else if (arg == L"--mode") {
            const wchar_t* v = next(L"--mode");
            int value = -1;
            if (!v || !ParseInt(v, &value) || (value != 0 && value != 1)) return false;
            opt->mode = value;
        } else if (arg == L"--wait") {
            const wchar_t* v = next(L"--wait");
            int value = 0;
            if (!v || !ParseInt(v, &value) || value < 0) return false;
            opt->wait_ms = static_cast<DWORD>(value);
        } else if (arg == L"--timeout") {
            const wchar_t* v = next(L"--timeout");
            int value = 0;
            if (!v || !ParseInt(v, &value) || value <= 0) return false;
            opt->timeout_ms = static_cast<DWORD>(value);
        } else if (arg == L"--no-inject") {
            opt->inject = false;
        } else if (arg == L"--quiet") {
            opt->quiet = true;
        } else if (arg == L"--help" || arg == L"-h") {
            opt->help = true;
        } else {
            fwprintf(stderr, L"unknown argument: %ls\n", arg.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    Options opt;
    if (!ParseArgs(argc, argv, &opt)) {
        Usage();
        return 2;
    }
    if (opt.help) {
        Usage();
        return 0;
    }

    if (opt.dll.empty()) {
        opt.dll = zzz::inject::JoinPath(zzz::inject::ExeDir(),
                                       L"zzz-touch-hook.dll");
    }
    // The Wine wrapper passes Z:\ paths; a raw unix path is converted so manual
    // invocations inside `wine cmd` also work.
    opt.dll = zzz::inject::NormalizeDllPath(opt.dll);

    if (!opt.quiet) {
        wprintf(L"zzz-touch-inject (wine=%hs)\n",
                zzz::inject::WineVersion().empty()
                    ? "native"
                    : zzz::inject::WineVersion().c_str());
        wprintf(L"  dll    = %ls\n", opt.dll.c_str());
    }

    if (!zzz::inject::FileExists(opt.dll)) {
        fwprintf(stderr, L"hook DLL not found: %ls\n", opt.dll.c_str());
        return 5;
    }

    if (opt.mode >= 0) {
        if (!zzz::inject::WriteModeFile(opt.dll, opt.mode)) {
            fwprintf(stderr, L"cannot write mode file next to %ls\n", opt.dll.c_str());
            return 6;
        }
        if (!opt.quiet) {
            wprintf(L"  mode   = %d (%ls) -> %ls\n", opt.mode,
                    opt.mode ? L"MOBILE" : L"DESKTOP",
                    zzz::inject::ModeFilePath(opt.dll).c_str());
        }
    }

    DWORD pid = opt.pid;
    if (!pid) {
        pid = opt.wait_ms > 0
                  ? zzz::inject::WaitForProcessId(opt.name, opt.wait_ms)
                  : zzz::inject::FindProcessIdByName(opt.name);
    }
    if (!pid) {
        fwprintf(stderr, L"target process not found: %ls\n", opt.name.c_str());
        return 3;
    }
    if (!opt.quiet) wprintf(L"  target = %ls (pid=%lu)\n", opt.name.c_str(), pid);

    if (!opt.inject) return 0;

    if (!zzz::inject::InjectDllByPid(pid, opt.dll, opt.timeout_ms)) {
        fwprintf(stderr, L"injection failed (pid=%lu)\n", pid);
        return 5;
    }
    if (!opt.quiet) wprintf(L"  injected OK\n");
    return 0;
}
