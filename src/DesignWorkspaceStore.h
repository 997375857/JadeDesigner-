#pragma once
#include "DesignerText.h"

// Design artifacts are sidecars of the saved .e project. Browser messages never
// choose a filesystem path and never write HTML, WPE or E-language source.
namespace DesignWorkspaceStore {
inline std::wstring Path(const std::wstring& project, bool task=false) {
    return project + (task ? L".jade-ai-task.md" : L".jade.design.json");
}
inline bool Read(const std::wstring& path, std::string& bytes, std::string& error) {
    bytes.clear();
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE) {
        if(GetLastError()==ERROR_FILE_NOT_FOUND)return true;
        error="设计文件无法读取，可能被占用";return false;
    }
    bool ok=DesignerText::AtExpectedPath(h,path)&&DesignerText::ReadHandle(h,bytes);
    CloseHandle(h);
    if(!ok||DesignerText::Wide(bytes).empty()){error="设计文件不是有效的 UTF-8 普通文件，或超过 2 MB";return false;}
    return true;
}
inline bool Save(const std::wstring& path,const std::string& expected,const std::string& next,std::string& error) {
    if(next.empty()||next.size()>1024*1024||DesignerText::Wide(next).empty()||next.find('\0')!=next.npos){error="设计内容无效或超过 1 MB";return false;}
    const bool creating=GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES;
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,
        creating?CREATE_NEW:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE){error="设计文件被占用或只读，未保存";return false;}
    std::string current;
    bool ok=DesignerText::AtExpectedPath(h,path)&&(creating||DesignerText::ReadHandle(h,current));
    if(!ok||current!=expected){CloseHandle(h);if(creating)DeleteFileW(path.c_str());error="设计文件已被外部修改，请重新打开设计页后合并";return false;}
    const auto put=[&](const std::string& value){LARGE_INTEGER zero{};DWORD n=0;
        return SetFilePointerEx(h,zero,nullptr,FILE_BEGIN)&&WriteFile(h,value.data(),static_cast<DWORD>(value.size()),&n,nullptr)&&n==value.size()&&SetEndOfFile(h)&&FlushFileBuffers(h);};
    ok=put(next);
    if(!ok){const bool restored=put(current);error=restored?"写入失败，已恢复原设计文件":"写入失败且恢复失败，请保留浏览器中的设计稿";}
    CloseHandle(h);if(!ok&&creating)DeleteFileW(path.c_str());return ok;
}
}
