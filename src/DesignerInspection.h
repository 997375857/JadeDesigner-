#pragma once
#include "PageSource.h"
#include <cstring>
#include <map>
#include <sstream>

namespace DesignerInspection {
inline bool Subscription(std::string text,std::string& channel,std::string& handler) {
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{
        {"（","("},{"）",")"},{"，",","},{"“","\""},{"”","\""}}) {
        size_t p=0; while((p=text.find(pair.first,p))!=text.npos) {text.replace(p,pair.first.size(),pair.second);p+=pair.second.size();}
    }
    std::string compact; bool quoted=false;
    for(char c:text) {
        if(c=='\"') quoted=!quoted;
        if(quoted || (c!=' '&&c!='\t'&&c!='\r'&&c!='\n')) compact+=c;
    }
    const std::string prefix="JadeView.通讯.订阅(\"";
    if(!compact.starts_with(prefix)) return false;
    const auto end=compact.find('"',prefix.size());
    if(end==compact.npos || compact.substr(end,3)!="\",&") return false;
    const auto close=compact.find(')',end+3);
    if(close==compact.npos || (close+1<compact.size()&&compact[close+1]!='\'')) return false;
    channel=compact.substr(prefix.size(),end-prefix.size());
    handler=compact.substr(end+3,close-end-3);
    return !channel.empty()&&!handler.empty()&&PageSource::LeadingIdentifier(handler)==handler;
}
struct Routine {
    std::string name, source;
    int parameters = 0;
    // Omitted trailing arguments must all be optional, not just any N arguments.
    int minimumArguments = 0;
};
inline bool OptionalParameter(std::string_view declaration) {
    // The third field contains flags; the fourth is a free-form description.
    for (int i = 0; i < 2; ++i) {
        const auto comma = declaration.find(',');
        if (comma == declaration.npos) return false;
        declaration.remove_prefix(comma + 1);
    }
    const auto flags = declaration.substr(0, declaration.find(','));
    std::istringstream input{std::string(flags)};
    std::string flag;
    while (input >> flag) if (flag == "可空") return true;
    return false;
}
inline std::vector<Routine> Routines(const std::string& source) {
    std::vector<Routine> out;
    std::istringstream input(source);
    std::string line;
    while (std::getline(input,line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first=line.find_first_not_of(" \t");
        const auto text = first==std::string::npos ? std::string() : line.substr(first);
        if (PageSource::HasDirective(text,".子程序")) out.push_back({PageSource::LeadingIdentifier(text.substr(std::string(".子程序").size())),{},0});
        if (!out.empty()) {
            out.back().source += line + "\n";
            if (PageSource::HasDirective(text,".参数")) {
                auto& routine = out.back();
                ++routine.parameters;
                if (!OptionalParameter(text)) routine.minimumArguments = routine.parameters;
            }
        }
    }
    return out;
}
inline int Count(const std::vector<Routine>& list, const std::string& name) {
    int n=0; for (const auto& r:list) if (_stricmp(r.name.c_str(),name.c_str())==0) ++n; return n;
}
inline bool Method(const std::string& source,const std::string& name,int arity) {
    const auto list=Routines(source);
    if (Count(list,name)!=1) return false;
    for (const auto& r:list) if (_stricmp(r.name.c_str(),name.c_str())==0) return r.parameters==arity;
    return false;
}
inline std::string CallProblem(const std::string& source,const std::string& name,int arity) {
    const auto list = Routines(source);
    const int count = Count(list, name);
    if (count == 0) return "未找到该方法";
    if (count != 1) return "存在同名方法，无法确定调用目标";
    for (const auto& routine : list) {
        if (_stricmp(routine.name.c_str(), name.c_str()) != 0) continue;
        if (arity < 0) return "调用参数数量不合法";
        if (arity > routine.parameters)
            return "模板传入 " + std::to_string(arity) + " 个参数，方法最多接受 " +
                std::to_string(routine.parameters) + " 个";
        if (arity < routine.minimumArguments)
            return "模板传入 " + std::to_string(arity) + " 个参数，但第 " +
                std::to_string(routine.minimumArguments) + " 个参数不可省略";
        return {};
    }
    return "未找到该方法";
}
}
