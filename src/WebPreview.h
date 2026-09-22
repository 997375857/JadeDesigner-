#pragma once

#include <Windows.h>

#include <string>

namespace WebPreview {

void InitializeModule(HMODULE module);
bool Attach(HWND mainWindow, HWND mdiClient, HWND codeTab, bool runtimeEnabled = true);
bool IsCurrentProjectWpe(HWND mainWindow, std::string* reason = nullptr);
void Toggle();
void Show();
void ShowDesign();
void Hide();
void Refresh();
void Layout();
bool IsActive();
bool IsAttached();
void Shutdown();

} // namespace WebPreview
