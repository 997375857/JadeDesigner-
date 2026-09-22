#pragma once
#include <string_view>

namespace ProjectWebDetection {
inline std::string_view Marker(std::string_view html)
{
    constexpr std::string_view markers[] = {
        "data-jade-control", "data-jade-channel", "data-jade-handler", "jade.invoke", "window.jade"
    };
    for (const auto marker : markers)
        if (html.find(marker) != std::string_view::npos) return marker;
    return {};
}
} // namespace ProjectWebDetection
