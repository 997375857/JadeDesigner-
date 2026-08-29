#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace DesignerLog {

void Initialize(HMODULE module);
void ResetForSession();
void Write(std::string_view message);
void WriteWide(std::wstring_view message);
std::string ToUtf8(std::wstring_view text);
std::string HexPointer(const void* value);

} // namespace DesignerLog
