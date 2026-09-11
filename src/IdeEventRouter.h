#pragma once

#include <Windows.h>

#include <string>
#include <string_view>
#include "CommonCode.h"
#include "ProjectHealth.h"

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
    // Optional non-modal confirmation, separate from the normal status ack.
    std::wstring notice;
};

// One encodeURIComponent-escaped field of a bridge message, as UTF-8.
std::string DecodeWireField(std::wstring_view value);
bool TryParseWebMessage(std::wstring_view wireMessage, UiEvent& event);
RouteResult Route(HWND mainWindow, HWND mdiClient, const UiEvent& event);
RouteResult GenerateCommonCode(HWND mainWindow, CommonCode::Options options = {});
struct BindingInfo { UiEvent normalized; std::string status, message; };
BindingInfo InspectBinding(const UiEvent& event);
RouteResult OperateBinding(HWND mainWindow, HWND mdiClient, const UiEvent& event, bool locateOnly);
struct CommonPreview { bool ready = false; CommonCode::Options options; std::string source, existing, report; };
CommonPreview PreviewCommonCode(HWND mainWindow, CommonCode::Options options);
std::vector<ProjectHealth::Check> InspectProjectHealth();
void ToggleNativeComponentBar();
std::wstring BuildAckMessage(const RouteResult& result);

} // namespace IdeEventRouter
