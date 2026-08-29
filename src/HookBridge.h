#pragma once

#include <string>

namespace HookBridge {

bool GenerateAssembly(
    const std::string& assemblyAnsi,
    const std::string& channelAnsi,
    const std::string& handlerAnsi,
    std::string& error);

bool InsertAnsi(const std::string& text, std::string& error);

} // namespace HookBridge
