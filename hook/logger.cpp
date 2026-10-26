#include "logger.h"

#include <windows.h>
#include <strsafe.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace zzz::log {
namespace {

std::mutex g_mu;
HANDLE g_file = INVALID_HANDLE_VALUE;
std::once_flag g_initFlag;

const char* LevelTag(Level level) {
    switch (level) {
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

SYSTEMTIME NowLocal() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return st;
}

void MakeTimestamp(char* out, size_t cap) {
    const auto st = NowLocal();
    StringCchPrintfA(out, cap,
        "%04u-%02u-%02u %02u:%02u:%02u.%03u",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

std::wstring MakeLogFilename() {
    const auto st = NowLocal();
    wchar_t buf[64]{};
    StringCchPrintfW(buf, 64, L"touch-%04u%02u%02u-%02u%02u%02u.log",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

bool EnsureLogDir(const std::wstring& root) {
    const std::wstring logs = root + L"\\logs";
    if (!CreateDirectoryW(logs.c_str(), nullptr)) {
        const DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS) return false;
    }

    const std::wstring path = logs + L"\\" + MakeLogFilename();
    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}

void WriteToFile(const char* line, size_t len, bool flush) {
    if (g_file == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(g_file, line, static_cast<DWORD>(len), &wrote, nullptr);
    if (flush) FlushFileBuffers(g_file);
}

}  // namespace

void Init(const wchar_t* root_dir) {
    std::call_once(g_initFlag, [&] {
        // ZZZ_TOUCH_LOG_DIR lets the Wine wrapper collect logs outside the
        // hook directory (the launcher itself never sets it).
        wchar_t override_dir[MAX_PATH]{};
        const DWORD override_len =
            GetEnvironmentVariableW(L"ZZZ_TOUCH_LOG_DIR", override_dir, MAX_PATH);
        const wchar_t* effective =
            override_len > 0 && override_len < MAX_PATH ? override_dir : root_dir;
        if (effective && *effective) {
            std::wstring root = effective;
            while (!root.empty() && (root.back() == L'\\' || root.back() == L'/'))
                root.pop_back();

            if (!EnsureLogDir(root)) {
                wchar_t tmp[MAX_PATH]{};
                GetTempPathW(MAX_PATH, tmp);
                std::wstring fallback = tmp;
                while (!fallback.empty() &&
                       (fallback.back() == L'\\' || fallback.back() == L'/')) {
                    fallback.pop_back();
                }
                EnsureLogDir(fallback);
            }
        }
        Write(Level::Info, "logger initialized");
    });
}

void Write(Level level, const char* fmt, ...) {
    char stamp[40]{};
    MakeTimestamp(stamp, sizeof(stamp));

    char body[2048]{};
    va_list ap;
    va_start(ap, fmt);
    StringCchVPrintfA(body, sizeof(body), fmt, ap);
    va_end(ap);

    char line[2200]{};
    StringCchPrintfA(line, sizeof(line), "[%s] [%s] [tid=%04lu] %s\r\n",
        stamp, LevelTag(level), GetCurrentThreadId(), body);

    std::lock_guard<std::mutex> lock(g_mu);
    if (g_file != INVALID_HANDLE_VALUE)
        WriteToFile(line, std::strlen(line), level >= Level::Warn);
    OutputDebugStringA(line);
}

}  // namespace zzz::log
