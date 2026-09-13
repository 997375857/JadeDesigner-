#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PageSource {

struct PageCodeInfo {
    bool valid = false;
    std::string assemblyUtf8;
    std::vector<std::string> subNamesUtf8;
    std::vector<int> subLinesFromText;
};

inline std::string LeadingIdentifier(std::string_view rest)
{
    const size_t begin = rest.find_first_not_of(" \t");
    if (begin == std::string_view::npos) return {};
    const size_t end = rest.find_first_of(", \t\r", begin);
    return std::string(rest.substr(begin, end == std::string_view::npos ? end : end - begin));
}

inline bool HasDirective(std::string_view line, std::string_view tag)
{
    return line.starts_with(tag) && line.size() > tag.size() &&
        (line[tag.size()] == ' ' || line[tag.size()] == '\t');
}

inline PageCodeInfo ParsePageCode(const std::string& source)
{
    PageCodeInfo info;
    constexpr std::string_view assemblyTag = ".程序集";
    constexpr std::string_view subTag = ".子程序";
    int assemblyHeaders = 0;
    int lineIndex = 0;
    for (size_t start = 0; start < source.size(); ++lineIndex) {
        size_t end = source.find('\n', start);
        if (end == std::string::npos) end = source.size();
        std::string_view line(source.data() + start, end - start);
        start = end + 1;
        const size_t indent = line.find_first_not_of(" \t");
        if (indent == std::string_view::npos) continue;
        line.remove_prefix(indent);
        // .程序集变量 is a different directive; prefix matching would replace
        // the assembly name with "变量" and misidentify an otherwise valid page.
        if (HasDirective(line, assemblyTag)) {
            ++assemblyHeaders;
            info.assemblyUtf8 = LeadingIdentifier(line.substr(assemblyTag.size()));
        }
        else if (HasDirective(line, subTag)) {
            std::string name = LeadingIdentifier(line.substr(subTag.size()));
            if (!name.empty()) {
                info.subNamesUtf8.push_back(std::move(name));
                info.subLinesFromText.push_back(lineIndex);
            }
        }
    }
    info.valid = assemblyHeaders == 1 && !info.assemblyUtf8.empty();
    return info;
}

} // namespace PageSource
