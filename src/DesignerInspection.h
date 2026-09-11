#pragma once
#include "PageSource.h"
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
struct Routine { std::string name, source; int parameters = 0; };
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
            if (PageSource::HasDirective(text,".参数")) ++out.back().parameters;
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
    for (const auto& r:list) if (r.name==name) return r.parameters==arity;
    return false;
}
}
