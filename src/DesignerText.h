#pragma once
#include <Windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <Shlwapi.h>
#pragma comment(lib,"Shlwapi.lib")

namespace DesignerText {
inline bool SameDocumentUrl(std::wstring source,std::wstring expected) {
    const auto fragment=source.find(L'#');
    if(fragment!=source.npos) source.resize(fragment);
    if(!source.starts_with(L"file:///") || !expected.starts_with(L"file:///")) return false;
    // WebView uses UTF-8 escapes while UrlCreateFromPathW may retain Unicode.
    // Decode exactly once, including %25; never treat a decoded percent as a
    // second escape sequence or a decoded # as a new fragment.
    const auto decode=[](std::wstring value) {
        std::wstring out(value.size()+1,L'\0'); DWORD size=static_cast<DWORD>(out.size());
        // URL_UNESCAPE_AS_UTF8 (Windows 8+); the plugin keeps older SDK target
        // macros for its host ABI, which hide this SDK constant.
        constexpr DWORD unescapeUtf8=0x00040000;
        if(FAILED(UrlUnescapeW(value.data(),out.data(),&size,unescapeUtf8))) return std::wstring();
        out.resize(size); return out;
    };
    const auto a=decode(source),b=decode(expected);
    return !a.empty() && a.find(L'\0')==a.npos && a==b;
}
inline std::wstring Wide(std::string_view bytes) {
    if(bytes.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(n<=0) return {};
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),out.data(),n); return out;
}
inline std::string Utf8(std::wstring_view s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(n<=0) return {};
    std::string out(n,'\0'); WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr); return out;
}
inline std::wstring Escape(std::wstring_view s) {
    std::wstring out; for(wchar_t c:s) {
        if(c==L'&') out+=L"&amp;"; else if(c==L'<') out+=L"&lt;"; else if(c==L'>') out+=L"&gt;";
        else out+=c;
    } return out;
}
// The browser uses parse5 for source locations. This additional lexical guard
// only permits text spans, never tag/attribute, comment or executable content.
inline bool TextRange(std::wstring_view s,size_t start,size_t end) {
    if(start>=end || end>s.size()) return false;
    std::vector<std::wstring> blocked;
    for(size_t p=0;p<s.size();) {
        if(s[p]!=L'<') {
            size_t q=s.find(L'<',p); if(q==s.npos) q=s.size();
            if(start>=p && end<=q) return blocked.empty();
            p=q; continue;
        }
        if(s.substr(p,4)==L"<!--") { size_t q=s.find(L"-->",p+4); if(q==s.npos) return false; p=q+3; continue; }
        size_t q=p+1; wchar_t quote=0;
        for(;q<s.size();++q) {
            if(quote) { if(s[q]==quote) quote=0; }
            else if(s[q]==L'\'' || s[q]==L'"') quote=s[q];
            else if(s[q]==L'>') break;
        }
        if(q==s.size() || (start<q+1 && end>p)) return false;
        size_t a=p+1; bool closing=a<s.size() && s[a]==L'/'; if(closing) ++a;
        size_t b=a; while(b<q && ((s[b]>=L'a'&&s[b]<=L'z') || (s[b]>=L'A'&&s[b]<=L'Z'))) ++b;
        std::wstring tag(s.substr(a,b-a)); for(auto& c:tag) c=static_cast<wchar_t>(towlower(c));
        const bool forbidden=tag==L"script"||tag==L"style"||tag==L"template"||tag==L"textarea"||tag==L"noscript"||tag==L"svg"||tag==L"math"||tag==L"iframe"||tag==L"xmp";
        if(forbidden) { if(closing) { if(!blocked.empty() && blocked.back()==tag) blocked.pop_back(); } else blocked.push_back(tag); }
        p=q+1;
    }
    return false;
}
inline bool ReadHandle(HANDLE file,std::string& bytes) {
    LARGE_INTEGER length{}; if(!GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>2*1024*1024) return false;
    bytes.resize(static_cast<size_t>(length.QuadPart)); DWORD n=0;
    return ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size();
}
inline bool AtExpectedPath(HANDLE file,const std::wstring& path) {
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) return false;
    std::vector<wchar_t> actual(32768),expected(32768),expanded(32768);
    const auto a=GetFinalPathNameByHandleW(file,actual.data(),static_cast<DWORD>(actual.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto b=GetFullPathNameW(path.c_str(),static_cast<DWORD>(expected.size()),expected.data(),nullptr);
    if(!a||a>=actual.size()||!b||b>=expected.size()) return false;
    const auto full=GetLongPathNameW(expected.data(),expanded.data(),static_cast<DWORD>(expanded.size()));
    if(!full||full>=expanded.size()) return false;
    std::wstring finalPath(actual.data(),a);
    if(finalPath.starts_with(L"\\\\?\\")) finalPath.erase(0,4);
    return _wcsicmp(finalPath.c_str(),expanded.data())==0;
}
struct Document {
    std::wstring path;
    std::string bytes, beforeUndo;
    unsigned revision=0;
    bool Open(const std::wstring& file,std::string& error) {
        const auto attrs=GetFileAttributesW(file.c_str());
        const auto dir=GetFileAttributesW(file.substr(0,file.find_last_of(L"\\/")).c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
            dir==INVALID_FILE_ATTRIBUTES || (dir&FILE_ATTRIBUTE_REPARSE_POINT)) { error="只支持工程 web 目录内的普通 HTML 文件"; return false; }
        HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(h==INVALID_HANDLE_VALUE) { error="网页文件被占用，暂时不能读取"; return false; }
        std::string content; bool ok=AtExpectedPath(h,file)&&ReadHandle(h,content); CloseHandle(h);
        if(!ok || Wide(content).empty()) { error="仅支持不超过 2 MB 的 UTF-8 HTML 文件，未转换原编码"; return false; }
        if(file!=path || bytes!=content) beforeUndo.clear();
        path=file; bytes=std::move(content); ++revision; return true;
    }
    bool WriteChecked(const std::string& next,std::string& error) {
        HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(h==INVALID_HANDLE_VALUE) { error="网页只读或正在被其他程序修改"; return false; }
        if(!AtExpectedPath(h,path)) { CloseHandle(h); error="文件路径已变化或不是普通文件，未写入"; return false; }
        std::string current;
        if(!ReadHandle(h,current)||current!=bytes) { CloseHandle(h); error="文件已被外部修改，请重新选择文字"; return false; }
        // Exclusive write/delete lock lasts through backup, write and rollback.
        const auto backup=path+L".jade-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(revision)+L".bak";
        HANDLE b=CreateFileW(backup.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr); DWORD n=0;
        bool saved=b!=INVALID_HANDLE_VALUE && WriteFile(b,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(b);
        if(b!=INVALID_HANDLE_VALUE) CloseHandle(b);
        if(!saved) { CloseHandle(h); error="备份失败，原网页未修改"; return false; }
        const auto put=[&](const std::string& value) {
            LARGE_INTEGER zero{}; DWORD written=0;
            return SetFilePointerEx(h,zero,nullptr,FILE_BEGIN)&&WriteFile(h,value.data(),static_cast<DWORD>(value.size()),&written,nullptr)&&
                written==value.size()&&SetEndOfFile(h)&&FlushFileBuffers(h);
        };
        const bool ok=put(next);
        if(!ok) { const bool restored=put(bytes); CloseHandle(h); error=restored?"写入失败，已恢复原文件":"写入失败，请从同目录 .jade-*.bak 恢复"; return false; }
        CloseHandle(h); beforeUndo=bytes; bytes=next; ++revision; return true;
    }
    bool Save(unsigned expected,size_t start,size_t end,const std::wstring& old,const std::wstring& replacement,std::string& error) {
        const auto source=Wide(bytes);
        if(expected!=revision || !TextRange(source,start,end) || source.substr(start,end-start)!=old ||
            replacement.size()>8192 ||
            std::any_of(replacement.begin(),replacement.end(),[](wchar_t c){return c<32 && c!=L'\r' && c!=L'\n' && c!=L'\t';})) {
            error="文字位置已过期或不是可修改的静态文字"; return false;
        }
        auto next=source; next.replace(start,end-start,Escape(replacement));
        const auto encoded=Utf8(next);
        if(encoded.empty()) { error="文字包含无效字符，原文件未修改"; return false; }
        return WriteChecked(encoded,error);
    }
    bool Undo(std::string& error) {
        if(beforeUndo.empty()) { error="当前会话没有可撤销的文字修改"; return false; }
        const auto target=beforeUndo; if(!WriteChecked(target,error)) return false; beforeUndo.clear(); return true;
    }
};
}
