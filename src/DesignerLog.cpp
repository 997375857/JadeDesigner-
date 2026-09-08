#include "DesignerLog.h"

#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::mutex g_logMutex;
std::wstring g_logPath;

// Every path this process has already truncated. Switching to another project
// and back must not wipe the log of the project being returned to.
std::vector<std::wstring> g_startedPaths;

// Lines written before the IDE has a project open - plugin attach, WebView2
// creation - have no project directory to go to yet. Holding them until the
// target is known keeps them in the one log the user reads instead of
// scattering a second file somewhere else, and the timestamp of each line is
// taken when it was written, so the flush stays in order. Bounded, because an
// IDE session that never opens a project must not grow this without limit.
std::string g_pendingLines;
size_t g_droppedPendingLines = 0;
constexpr size_t kMaxPendingBytes = 512 * 1024;

constexpr wchar_t kLogFileName[] = L"JadeDesigner.log";

std::wstring DirectoryOf(std::wstring_view path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? std::wstring() : std::wstring(path.substr(0, slash));
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

void AppendToFile(const std::wstring& path, const void* data, DWORD size)
{
    if (path.empty() || data == nullptr || size == 0) {
        return;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
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

void HoldLineLocked(const std::string& line)
{
    g_pendingLines += line;
    if (g_pendingLines.size() <= kMaxPendingBytes) {
        return;
    }
    // Drop from the front, whole lines at a time: the newest lines are the ones
    // that explain what just happened.
    size_t cut = 0;
    while (g_pendingLines.size() - cut > kMaxPendingBytes / 2) {
        const size_t next = g_pendingLines.find("\r\n", cut);
        if (next == std::string::npos) {
            break;
        }
        cut = next + 2;
        ++g_droppedPendingLines;
    }
    g_pendingLines.erase(0, cut);
}

// One log per project directory, truncated the first time this process targets
// it, so a fresh IDE run starts a fresh log without ever accumulating a second
// file.
bool StartFileLocked(const std::wstring& path)
{
    for (const std::wstring& started : g_startedPaths) {
        if (_wcsicmp(started.c_str(), path.c_str()) == 0) {
            return true;
        }
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    constexpr unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    WriteFile(file, bom, sizeof(bom), &written, nullptr);
    CloseHandle(file);
    g_startedPaths.push_back(path);
    return true;
}

void EmitLineLocked(const std::string& message)
{
    const std::string line = "[" + BuildTimestamp() + "] " + message + "\r\n";
    if (g_logPath.empty()) {
        HoldLineLocked(line);
        return;
    }
    AppendToFile(g_logPath, line.data(), static_cast<DWORD>(line.size()));
}

} // namespace

namespace DesignerLog {

void Initialize(HMODULE module)
{
    // The log belongs next to the source being worked on, and which project
    // that is only becomes known once the IDE has opened one. Nothing to set
    // up here; Write holds its lines until UseProjectFile names the target.
    (void)module;
}

void UseProjectFile(std::wstring_view projectFilePath)
{
    const std::wstring directory = DirectoryOf(projectFilePath);
    if (directory.empty()) {
        return;
    }
    const std::wstring path = directory + L"\\" + kLogFileName;

    std::lock_guard lock(g_logMutex);
    if (_wcsicmp(path.c_str(), g_logPath.c_str()) == 0) {
        return;
    }
    // A project directory that cannot be written to must not swallow the log:
    // keep holding the lines so a later, writable target still receives them.
    if (!StartFileLocked(path)) {
        return;
    }

    const std::wstring previous = g_logPath;
    g_logPath = path;
    if (!g_pendingLines.empty()) {
        AppendToFile(g_logPath, g_pendingLines.data(), static_cast<DWORD>(g_pendingLines.size()));
        g_pendingLines.clear();
    }

    std::string note = "LOG target=\"" + ToUtf8(path) + "\"";
    if (!previous.empty()) {
        note += " previous=\"" + ToUtf8(previous) + "\"";
    }
    if (g_droppedPendingLines > 0) {
        note += " dropped_early_lines=" + std::to_string(g_droppedPendingLines);
        g_droppedPendingLines = 0;
    }
    EmitLineLocked(note);
}

void Write(std::string_view message)
{
    std::lock_guard lock(g_logMutex);
    EmitLineLocked(std::string(message));
}

void WriteFromFault(std::string_view message)
{
    std::unique_lock<std::mutex> lock(g_logMutex, std::try_to_lock);
    if (lock.owns_lock()) {
        EmitLineLocked(std::string(message));
        return;
    }
    // The lock is held, possibly by this very thread if it faulted inside the
    // logger. Waiting would hang the IDE instead of letting it report and die,
    // which is the worse outcome, so read the target without the lock and write
    // straight out; an interleaved line still says where the fault was.
    if (g_logPath.empty()) {
        return;
    }
    const std::string line = "[" + BuildTimestamp() + "] " + std::string(message) + "\r\n";
    AppendToFile(g_logPath, line.data(), static_cast<DWORD>(line.size()));
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
