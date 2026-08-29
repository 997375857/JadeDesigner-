#pragma once

#include <Windows.h>

#include <string>
#include <string_view>

namespace IdeEventRouter {

struct UiEvent {
    std::string domEvent;
    std::string controlType;
    std::string elementId;
    std::string value;
    std::string handlerName;
    std::string assemblyName;
    bool checked = false;
    // data-jade-call: "JadeView.通讯.订阅" / "JadeView.App.注册事件" / empty
    std::string callType;
    // data-jade-channel (subscribe) or data-jade-event (register)
    std::string callParam;
};

struct RouteResult {
    bool succeeded = false;
    std::string action;
    std::string message;
};

bool TryParseWebMessage(std::wstring_view wireMessage, UiEvent& event);
RouteResult Route(HWND mainWindow, HWND mdiClient, const UiEvent& event);
std::wstring BuildAckMessage(const RouteResult& result);

} // namespace IdeEventRouter
