#pragma once
#include "DesignerVisual.h"
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

// Reuse the editor's tag reader; never execute a page to classify a project.
inline std::vector<std::wstring> ScriptReferences(std::string_view html)
{
    const auto source = DesignerText::Wide(html);
    const auto lower = DesignerVisual::Lower(source);
    std::vector<std::wstring> references;
    unsigned templateDepth = 0;
    for (size_t p = 0; p < source.size();) {
        p = source.find(L'<', p);
        if (p == source.npos) break;
        if (source.compare(p, 4, L"<!--") == 0) {
            const auto end = source.find(L"-->", p + 4);
            if (end == source.npos) break;
            p = end + 3;
            continue;
        }
        DesignerVisual::Tag tag;
        if (!DesignerVisual::ParseTag(source, p, tag)) { ++p; continue; }
        p = tag.end;
        if (tag.name == L"template") {
            if (!tag.closing) ++templateDepth;
            else if (templateDepth) --templateDepth;
        }
        if (tag.closing) continue;
        // A base URL changes script resolution. Do not guess a local mapping.
        if (!templateDepth && tag.name == L"base") {
            for (const auto& attr : tag.attrs)
                if (attr.name == L"href") return {};
        }
        if (!templateDepth && tag.name == L"script" && references.size() < 32) {
            std::wstring src, type;
            for (const auto& attr : tag.attrs) {
                if (attr.name == L"src") src = source.substr(attr.valueStart, attr.valueEnd - attr.valueStart);
                if (attr.name == L"type") type = DesignerVisual::Lower(source.substr(attr.valueStart, attr.valueEnd - attr.valueStart));
            }
            if (!src.empty() && (type.empty() || type == L"module" ||
                type == L"text/javascript" || type == L"application/javascript"))
                references.push_back(std::move(src));
        }
        if (DesignerVisual::Raw(tag.name)) {
            const auto closing = L"</" + tag.name;
            auto end = lower.find(closing, p);
            while (end != source.npos) {
                const auto after = end + closing.size();
                if (after == source.size() || source[after] == L'>' ||
                    source[after] == L'/' || DesignerVisual::Space(source[after])) break;
                end = lower.find(closing, after);
            }
            if (end == source.npos) break;
            p = end;
        }
    }
    return references;
}

inline std::wstring LocalScriptPath(const std::wstring& webDirectory, std::wstring reference)
{
    const auto first = reference.find_first_not_of(L" \t\r\n\f");
    if (first == reference.npos) return {};
    reference = reference.substr(first, reference.find_last_not_of(L" \t\r\n\f") - first + 1);
    const auto suffix = reference.find_first_of(L"?#");
    if (suffix != reference.npos) reference.resize(suffix);
    std::wstring decoded(reference.size() + 1, L'\0');
    DWORD length = static_cast<DWORD>(decoded.size());
    if (FAILED(UrlUnescapeW(reference.data(), decoded.data(), &length, 0x00040000))) return {};
    decoded.resize(length);
    if (decoded.empty() || decoded.front() == L'/' || decoded.front() == L'\\' ||
        decoded.find_first_of(L":&<>\"|?*") != decoded.npos ||
        std::any_of(decoded.begin(), decoded.end(), [](wchar_t c) { return c < 32; })) return {};
    const auto fullPath = [](const std::wstring& path) {
        std::wstring full(32768, L'\0');
        const auto n = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (!n || n >= full.size()) return std::wstring();
        full.resize(n);
        return full;
    };
    auto root = fullPath(webDirectory);
    if (root.empty()) return {};
    if (root.back() != L'\\') root += L'\\';
    const auto path = fullPath(root + decoded);
    if (path.size() <= root.size() || _wcsnicmp(path.c_str(), root.c_str(), root.size()) != 0) return {};
    return path;
}

inline bool Detect(std::string_view html, const std::wstring& webDirectory, std::string& reason)
{
    if (const auto marker = Marker(html); !marker.empty()) {
        reason = "marker=" + std::string(marker);
        return true;
    }
    // Bound polling work and inspect only files actually referenced by this page.
    size_t remaining = 2 * 1024 * 1024;
    std::set<std::wstring> visited;
    for (const auto& reference : ScriptReferences(html)) {
        const auto path = LocalScriptPath(webDirectory, reference);
        if (path.empty() || !visited.insert(DesignerVisual::Lower(path)).second) continue;
        if (!remaining) break;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size{};
        std::string content;
        if (DesignerText::AtExpectedPath(file, path) && GetFileSizeEx(file, &size) && size.QuadPart > 0) {
            const auto limit = (std::min)(remaining, size_t{512 * 1024});
            const DWORD requested = static_cast<DWORD>((std::min)(size.QuadPart, static_cast<LONGLONG>(limit)));
            remaining -= requested;
            content.resize(requested);
            DWORD read = 0;
            if (ReadFile(file, content.data(), requested, &read, nullptr)) content.resize(read);
            else content.clear();
        }
        CloseHandle(file);
        const auto marker = Marker(content);
        if (!marker.empty() || content.find("jade.on(") != content.npos) {
            reason = "script=" + DesignerText::Utf8(path) +
                " marker=" + (marker.empty() ? "jade.on" : std::string(marker));
            return true;
        }
    }
    reason = "jade_marker_missing";
    return false;
}
} // namespace ProjectWebDetection
