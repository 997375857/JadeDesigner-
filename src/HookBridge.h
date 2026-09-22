#pragma once

#include <string>
#include <vector>

namespace HookBridge {

enum class BackgroundChange { Unknown, CallbackCreated, SubscriptionRepaired };

int EnsureCommon(const std::string& name, const std::string& source,
    const std::string& subscription, const std::string& fixed, std::string& error);

// Result: 1 created/repaired without activating a code page, 2 already complete,
// -1 refused/failed. All strings are GBK; generated statements are verified.
int EnsureBackground(const std::string& callbackAssembly, const std::string& assembly, const std::string& fixed,
    const std::string& handler, const std::string& statement,
    const std::string& callback, std::string& error, BackgroundChange& change);

// jadehook.dll is compiled against one specific IDE build. Before doing
// anything it checks that the host process is named e5.95.exe and that the
// editor entry point it hooks still starts with the expected bytes, and when
// either check fails it reports "generate_invalid_arguments" - the same reason
// it gives for a null argument. Mirroring both checks here is what lets a
// failure name the actual cause instead of pointing at the DLL.
struct HostInfo {
    std::string exeNameUtf8;
    bool nameSupported = false;
    bool buildSupported = false;

    bool ok() const { return nameSupported && buildSupported; }
};

HostInfo InspectHost();

// 1: unique assembly read; 0: proven absent; -1: unavailable/ambiguous.
int ReadAssembly(const std::string& nameAnsi, std::string& sourceUtf8, std::string& error);
int ReadRoutine(const std::string& nameAnsi, std::string& sourceUtf8, std::string& error);
// Reads every assembly name directly from the IDE memory model. This remains
// usable while the corresponding code page is closed or the project tree is
// waiting for a repaint.
std::vector<std::wstring> ListAssemblyNames(std::string& error);

bool GenerateAssembly(
    const std::string& assemblyAnsi,
    const std::string& channelAnsi,
    const std::string& handlerAnsi,
    std::string& error);

bool InsertAnsi(const std::string& text, std::string& error);

// Reads the whole source text of the active code page as UTF-8. Unlike a
// FN_GET_PRG_TEXT cell walk, which only answers for the rows the grid has
// materialised, this covers the entire page - which is what makes it usable for
// deciding whether a subroutine or statement is already there.
bool ReadPageCode(std::string& pageUtf8, std::string& error);

} // namespace HookBridge
