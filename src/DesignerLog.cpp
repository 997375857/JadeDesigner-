#include "DesignerLog.h"

#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace {

std::mutex g_logMutex;
std::wstring g_logPath;
bool g_sessionReset = false;

std::wstring ResolveModuleDirectory(HMODULE module)
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L".";
    }

    std::wstring path(buffer.data(), length);
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::string BuildTimestamp()
{
    SYSTEMTIME value{};
    GetLocalTime(&value);
    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(4) << value.wYear << '-'
           << std::setw(2) << value.wMonth << '-'
           << std::setw(2) << value.wDay << ' '
           << std::setw(2) << value.wHour << ':'
           << std::setw(2) << value.wMinute << ':'
           << std::setw(2) << value.wSecond << '.'
           << std::setw(3) << value.wMilliseconds;
    return stream.str();
}

void WriteBytesLocked(const void* data, DWORD size)
{
    if (g_logPath.empty() || data == nullptr || size == 0) {
        return;
    }

    HANDLE file = CreateFileW(
        g_logPath.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    DWORD written = 0;
    WriteFile(file, data, size, &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}

} // namespace

namespace DesignerLog {

void Initialize(HMODULE module)
{
    std::lock_guard lock(g_logMutex);
    if (g_logPath.empty()) {
        g_logPath = ResolveModuleDirectory(module) + L"\\JadeDesigner.log";
    }
}

void ResetForSession()
{
    std::lock_guard lock(g_logMutex);
    if (g_logPath.empty() || g_sessionReset) {
        return;
    }

    HANDLE file = CreateFileW(
        g_logPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        constexpr unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        DWORD written = 0;
        WriteFile(file, bom, sizeof(bom), &written, nullptr);
        CloseHandle(file);
        g_sessionReset = true;
    }
}

void Write(std::string_view message)
{
    std::lock_guard lock(g_logMutex);
    const std::string line = "[" + BuildTimestamp() + "] " + std::string(message) + "\r\n";
    WriteBytesLocked(line.data(), static_cast<DWORD>(line.size()));
}

void WriteWide(std::wstring_view message)
{
    Write(ToUtf8(message));
}

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
    return result;
}

std::string HexPointer(const void* value)
{
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex
           << std::setw(sizeof(void*) * 2) << std::setfill('0')
           << reinterpret_cast<std::uintptr_t>(value);
    return stream.str();
}

} // namespace DesignerLog
