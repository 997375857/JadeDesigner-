#pragma once
#include "DesignerText.h"

namespace DiagnosticsInstall {
inline constexpr wchar_t FileName[]=L"jade-designer-diagnostics-v1.js";
inline constexpr wchar_t Tag[]=L"\n<script src=\"./jade-designer-diagnostics-v1.js\" data-jade-diagnostics=\"1\"></script>\n";
// Accept only an explicit head before any application content. A malformed or
// implicit head is not a safe place to install a script ahead of business JS.
inline size_t HeadEnd(const std::wstring& source) {
    size_t p=0;
    if(!source.empty()&&source[0]==0xFEFF)++p;
    while(p<source.size()) {
        while(p<source.size()&&iswspace(source[p]))++p;
        if(source.compare(p,4,L"<!--")==0){const auto end=source.find(L"-->",p+4);if(end==source.npos)return source.npos;p=end+3;continue;}
        if(p>=source.size()||source[p]!=L'<')return source.npos;
        size_t end=p+1;wchar_t quote=0;
        for(;end<source.size();++end){const auto c=source[end];if(quote){if(c==quote)quote=0;}else if(c==L'\''||c==L'"')quote=c;else if(c==L'>')break;}
        if(end==source.size())return source.npos;
        size_t n=p+1;while(n<end&&!iswspace(source[n])&&source[n]!=L'/')++n;
        auto name=source.substr(p+1,n-p-1);for(auto& c:name)c=static_cast<wchar_t>(towlower(c));
        if(name==L"head"&&source[end-1]!=L'/')return end+1;
        if(name!=L"html"&&name!=L"!doctype")return source.npos;
        p=end+1;
    }
    return source.npos;
}
inline bool Install(const std::wstring& html,const std::string& script,std::string& message) {
    DesignerText::Document document;
    if(!document.Open(html,message))return false;
    auto source=DesignerText::Wide(document.bytes);
    const auto pos=HeadEnd(source);
    if(pos==source.npos){message="需要显式的 <head>，未修改 HTML";return false;}
    const bool installed=source.compare(pos,std::wstring_view(Tag).size(),Tag)==0;
    if(!installed&&source.find(FileName)!=source.npos){message="网页已有诊断脚本引用，请先核对，未重复安装";return false;}
    const auto path=html.substr(0,html.find_last_of(L"\\/")+1)+FileName;
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,0,nullptr);
    if(h!=INVALID_HANDLE_VALUE){
        DWORD written=0;
        const bool ok=DesignerText::AtExpectedPath(h,path)&&WriteFile(h,script.data(),static_cast<DWORD>(script.size()),&written,nullptr)&&written==script.size()&&FlushFileBuffers(h);
        CloseHandle(h);
        if(!ok){message="诊断脚本写入失败，HTML 未修改；请检查同目录诊断脚本";return false;}
    } else {
        h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        std::string existing;
        const bool same=h!=INVALID_HANDLE_VALUE&&DesignerText::AtExpectedPath(h,path)&&DesignerText::ReadHandle(h,existing)&&existing==script;
        if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
        if(!same){message="同名诊断脚本存在且内容不同，未覆盖";return false;}
    }
    if(!installed){source.insert(pos,Tag);if(!document.WriteChecked(DesignerText::Utf8(source),message))return false;}
    message=installed?"运行诊断已接入，未重复修改网页。":"运行诊断已接入并备份 HTML；需在实际程序重新加载网页，不代表程序已经通过运行测试。";
    return true;
}
}
