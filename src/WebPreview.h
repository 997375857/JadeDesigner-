#pragma once

#include <Windows.h>

namespace WebPreview {

void InitializeModule(HMODULE module);
bool Attach(HWND mainWindow, HWND mdiClient, HWND codeTab);
void Toggle();
void Show();
void Hide();
void Refresh();
void Layout();
bool IsActive();
bool IsAttached();
void Shutdown();

} // namespace WebPreview
