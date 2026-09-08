#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace DesignerLog {

void Initialize(HMODULE module);
// Points the log at <project dir>\JadeDesigner.log, given the full path of the
// open .e file, and flushes whatever was written before the project was known.
// Cheap to call repeatedly; only the first call for a path does any work.
void UseProjectFile(std::wstring_view projectFilePath);
void Write(std::string_view message);
// Safe to call from a fault handler: never blocks on the log lock, because the
// thread that faulted may be the one holding it.
void WriteFromFault(std::string_view message);
void WriteWide(std::wstring_view message);
std::string ToUtf8(std::wstring_view text);
std::string HexPointer(const void* value);

} // namespace DesignerLog
