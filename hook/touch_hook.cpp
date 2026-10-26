// touch_hook.cpp — pattern-only native-touch runtime initialization.
#include "touch_hook.h"

#include "layout_platform_override.h"
#include "legacy_touch_restore.h"
#include "logger.h"

#include <windows.h>

#include <string>

namespace zzz::touch {
namespace {

// Wine attach path: the wrapper cannot pass ZZZ_TOUCH_MODE into an already
// running game, so it drops the mode next to the hook DLL and the hook picks it
// up from there. Returns false when the file is absent or malformed.
bool ReadModeFile(const std::wstring& unlocker_dir, wchar_t* mode) {
    std::wstring path = unlocker_dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    path += L"zzz-touch-mode.txt";

    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    char buffer[16]{};
    DWORD read = 0;
    const bool ok = ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok) return false;
    for (DWORD i = 0; i < read; ++i) {
        if (buffer[i] == '0' || buffer[i] == '1') {
            mode[0] = static_cast<wchar_t>(buffer[i]);
            return true;
        }
        if (buffer[i] != ' ' && buffer[i] != '\r' && buffer[i] != '\n' &&
            buffer[i] != '\t') {
            return false;
        }
    }
    return false;
}

std::string WineVersionForLog() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using WineGetVersionFn = const char*(__cdecl*)();
    auto fn = ntdll ? reinterpret_cast<WineGetVersionFn>(
                          GetProcAddress(ntdll, "wine_get_version"))
                    : nullptr;
    const char* version = fn ? fn() : nullptr;
    return version ? std::string(version) : std::string("native");
}

void SignalLauncherReady() {
    const DWORD pid = GetCurrentProcessId();
    wchar_t readyName[128]{};
    swprintf_s(readyName, L"Local\\ZZZTouchHookReady_%lu", pid);
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName);
    if (ready) {
        SetEvent(ready);
        CloseHandle(ready);
        LOG_INFO("launcher ready event signaled (pid=%lu)", pid);
    }
}

// Locator polling budget. A real client finds its patterns within a couple of
// seconds; the ceiling only exists so a patched/stale build still terminates
// instead of spinning forever. Overridable for tests.
DWORD FinalizeTimeoutMs() {
    wchar_t buffer[32]{};
    const DWORD length =
        GetEnvironmentVariableW(L"ZZZ_TOUCH_FINALIZE_TIMEOUT_MS", buffer, 32);
    if (length == 0 || length >= 32) {
        return 60 * 1000;
    }
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(buffer, &end, 10);
    if (value < 1000UL) {
        return 60 * 1000;
    }
    if (value > 600000UL) {
        return 600000UL;
    }
    return static_cast<DWORD>(value);
}

}  // namespace

void Run(const std::wstring& unlocker_dir) {
    zzz::log::Init(unlocker_dir.c_str());
    LOG_INFO("=========================================");
    LOG_INFO("zzz-touch-hook starting (pattern-only)");
    LOG_INFO("=========================================");

    wchar_t mode[2]{};
    const char* modeSource = "ENV";
    const DWORD length = GetEnvironmentVariableW(L"ZZZ_TOUCH_MODE", mode, 2);
    if (length != 1 || (mode[0] != L'0' && mode[0] != L'1')) {
        modeSource = "FILE";
        if (!ReadModeFile(unlocker_dir, mode)) {
            LOG_ERROR("NTR_ABORT missing/invalid launcher mode (env+file) dir=%ls",
                      unlocker_dir.c_str());
            return;
        }
    }
    const bool mobile = mode[0] == L'1';
    const int layout = mobile ? 1 : 2;  // Game enum: Mobile=1, PC=2.
    LOG_INFO("NTR_HOST wine=%s modeSource=%s dllDir=%ls",
             WineVersionForLog().c_str(), modeSource, unlocker_dir.c_str());
    LOG_INFO("NTR_MODE mode=%d target=%s liveLayoutPlatform=%d",
             mobile ? 1 : 0, mobile ? "MOBILE" : "DESKTOP", layout);
    const bool inputArmed = !mobile || zzz::legacy::ArmEarlyRestore();
    const bool layoutArmed = zzz::layout_override::ArmEarlyUi(layout);
    if (!inputArmed || !layoutArmed) {
        LOG_ERROR("NTR_ABORT early override arm failed input=%d layout=%d",
                  inputArmed ? 1 : 0, layoutArmed ? 1 : 0);
        return;  // no ready signal: launcher fails closed
    }
    SignalLauncherReady();

    // Establish the selected layout independently of saved preferences.
    const DWORD finalizeMs = FinalizeTimeoutMs();
    if (!zzz::layout_override::FinalizeRuntime(finalizeMs)) {
        LOG_ERROR("NTR_ABORT layout finalize failed");
        return;
    }
    // Legacy manager validation completes after Unity's main thread resumes.
    if (mobile && !zzz::legacy::FinalizeRuntime(finalizeMs)) {
        LOG_ERROR("NTR_ABORT input finalize failed");
        return;
    }

    LOG_INFO("NTR_READY touchInput=%s liveLayoutPlatform=%d "
             "persistPolicy=GAME_DEFAULT locators=PATTERN_ONLY "
             "layoutFieldOffset=0x%X fileMutationByUnlocker=NONE",
             mobile ? "LEGACY_NATIVE" : "GAME_DEFAULT", layout,
             static_cast<unsigned>(zzz::layout_override::LayoutFieldOffset()));
}

}  // namespace zzz::touch
