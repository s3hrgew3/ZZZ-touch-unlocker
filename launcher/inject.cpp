// inject.cpp — process lookup, Wine detection and remote LoadLibraryW
// injection. Kept free of any policy: callers decide whether injection is
// allowed in their environment.
#include "inject.h"

#include <tlhelp32.h>

#include <cstdio>
#include <cstring>

namespace zzz::inject {
namespace {

constexpr wchar_t kHookDllName[] = L"zzz-touch-hook.dll";
constexpr wchar_t kModeFileName[] = L"zzz-touch-mode.txt";

bool SameName(const wchar_t* a, const wchar_t* b) {
    return a && b && _wcsicmp(a, b) == 0;
}

}  // namespace

std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return DirOf(path);
}

std::wstring DirOf(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    const wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L"\\" + name;
}

bool FileExists(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsWine() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

std::string WineVersion() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return {};
    using WineGetVersionFn = const char*(__cdecl*)();
    auto fn = reinterpret_cast<WineGetVersionFn>(
        GetProcAddress(ntdll, "wine_get_version"));
    if (!fn) return {};
    const char* version = fn();
    return version ? std::string(version) : std::string();
}

std::wstring NormalizeDllPath(const std::wstring& path) {
    if (path.empty()) return path;
    // Already a Windows path: "C:\x", "Z:\x", "\\server\share\x".
    if (path.find(L':') != std::wstring::npos ||
        path.compare(0, 2, L"\\\\") == 0) {
        return path;
    }
    // Wine maps the host root to Z:.
    std::wstring out = L"Z:";
    for (wchar_t c : path) out += (c == L'/') ? L'\\' : c;
    return out;
}

DWORD FindProcessIdByName(const std::wstring& exe_name) {
    if (exe_name.empty()) return 0;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD found = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (SameName(entry.szExeFile, exe_name.c_str())) {
                found = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

DWORD WaitForProcessId(const std::wstring& exe_name, DWORD timeout_ms, DWORD poll_ms) {
    if (poll_ms == 0) poll_ms = 25;
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        const DWORD pid = FindProcessIdByName(exe_name);
        if (pid) return pid;
        if (GetTickCount64() - start >= timeout_ms) return 0;
        Sleep(poll_ms);
    }
}

bool InjectDllByHandle(HANDLE process, const std::wstring& dll_path, DWORD timeout_ms) {
    if (!process || dll_path.empty()) return false;

    const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(process, nullptr, bytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        wprintf(L"VirtualAllocEx failed: %lu\n", GetLastError());
        return false;
    }

    if (!WriteProcessMemory(process, remote, dll_path.c_str(), bytes, nullptr)) {
        wprintf(L"WriteProcessMemory failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr);
    if (!loadLibrary) {
        wprintf(L"GetProcAddress(LoadLibraryW) failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibrary,
                                       remote, 0, nullptr);
    if (!thread) {
        wprintf(L"CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        wprintf(L"[inject] LoadLibraryW wait failed/timeout: %lu\n", wait);
        // The remote thread may still be reading the path; leave the allocation
        // intact so the target cannot read freed memory.
        CloseHandle(thread);
        return false;
    }

    DWORD code = 0;
    const bool gotExit = GetExitCodeThread(thread, &code) != FALSE;
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);

    if (!gotExit || code == 0 || code == STILL_ACTIVE) {
        wprintf(L"[inject] LoadLibraryW failed, exit=0x%08lX gle=%lu\n",
                code, GetLastError());
        return false;
    }

    wprintf(L"[inject] LoadLibraryW succeeded (hmodule=0x%08lX)\n", code);
    return true;
}

bool InjectDllByPid(DWORD pid, const std::wstring& dll_path, DWORD timeout_ms) {
    if (!pid) return false;
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                                     PROCESS_VM_READ,
                                 FALSE, pid);
    if (!process) {
        wprintf(L"OpenProcess(pid=%lu) failed: %lu\n", pid, GetLastError());
        return false;
    }
    const bool ok = InjectDllByHandle(process, dll_path, timeout_ms);
    CloseHandle(process);
    return ok;
}

std::wstring ModeFilePath(const std::wstring& dll_path) {
    return JoinPath(DirOf(dll_path), kModeFileName);
}

bool WriteModeFile(const std::wstring& dll_path, int mode) {
    if (mode != 0 && mode != 1) return false;
    const std::wstring path = ModeFilePath(dll_path);
    if (path.empty()) return false;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        wprintf(L"cannot write mode file: %ls (%lu)\n", path.c_str(), GetLastError());
        return false;
    }
    const char text[2] = {static_cast<char>('0' + mode), '\n'};
    DWORD wrote = 0;
    const bool ok = WriteFile(file, text, 1, &wrote, nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}

}  // namespace zzz::inject
