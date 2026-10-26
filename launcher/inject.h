// inject.h — process lookup and DLL injection primitives shared by the
// Windows launcher (`zzz-touch-unlocker.exe`) and the attach-mode injector
// (`zzz-touch-inject.exe`, used by the Wine wrapper).
#pragma once

#include <windows.h>

#include <string>

namespace zzz::inject {

// How long the remote LoadLibraryW thread is allowed to run before the caller
// treats the injection as failed.
constexpr DWORD kDefaultInjectTimeoutMs = 15000;

// Windows path helpers. Under Wine these operate on Z:\ style paths.
std::wstring ExeDir();
std::wstring DirOf(const std::wstring& path);
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
bool FileExists(const std::wstring& path);

// Wine detection: ntdll exports wine_get_version only inside Wine.
bool IsWine();
std::string WineVersion();  // empty when not running under Wine

// Converts a host-style path ("/home/user/x/y.dll") into a Wine path
// ("Z:\home\user\x\y.dll"). Paths that already look like Windows paths are
// returned unchanged, so this is safe to call on both platforms.
std::wstring NormalizeDllPath(const std::wstring& path);

// Toolhelp-based lookup by image basename ("ZenlessZoneZero.exe"). Matching is
// case-insensitive and ignores directories. Returns 0 when absent.
DWORD FindProcessIdByName(const std::wstring& exe_name);

// Polls until the process shows up. Returns 0 on timeout.
DWORD WaitForProcessId(const std::wstring& exe_name,
                       DWORD timeout_ms,
                       DWORD poll_ms = 25);

// LoadLibraryW injection into an already-opened process. The process must have
// been opened with PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
// PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION.
bool InjectDllByHandle(HANDLE process,
                       const std::wstring& dll_path,
                       DWORD timeout_ms = kDefaultInjectTimeoutMs);

bool InjectDllByPid(DWORD pid,
                    const std::wstring& dll_path,
                    DWORD timeout_ms = kDefaultInjectTimeoutMs);

// The hook reads its mode from ZZZ_TOUCH_MODE, then from this file next to the
// DLL. Returned path is "<dll dir>\zzz-touch-mode.txt".
std::wstring ModeFilePath(const std::wstring& dll_path);
bool WriteModeFile(const std::wstring& dll_path, int mode);

}  // namespace zzz::inject
