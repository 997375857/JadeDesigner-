#pragma once

#include <Windows.h>

namespace IDEIntegration {

void InitializeModule(HMODULE module);
bool Start(HWND mainWindow);
void TogglePreview();
void RefreshPreview();
void Stop();

} // namespace IDEIntegration
