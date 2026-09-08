#pragma once

#include <windows.h>
#include <string>
#include <string_view>

namespace ProjectAssembly {

enum class State { Unavailable, Absent, Found, Ambiguous };

struct Lookup {
    State state = State::Unavailable;
    HWND tree = nullptr;
    LPARAM item = 0;
    HWND document = nullptr;
    int matches = 0;
    int nodes = 0;
    std::string reason;
};

bool MatchesDocumentTitle(std::wstring_view title, std::wstring_view name);
HWND FindOpenDocument(HWND mdiClient, std::wstring_view name);
// Reads the IDE's program tree, including collapsed nodes. No file parsing,
// clipboard access, window-title cache, or editor scrolling is involved.
Lookup Find(HWND mainWindow, std::wstring_view name);
// Existing-but-closed must never fall through to creation if opening fails.
Lookup FindAndOpen(HWND mainWindow, HWND mdiClient, std::wstring_view name);
bool JumpToSubroutine(HWND mainWindow, HWND mdiClient,
    std::wstring_view assembly, std::wstring_view subroutine);

} // namespace ProjectAssembly
