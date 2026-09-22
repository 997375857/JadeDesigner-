#pragma once
#include <Windows.h>
#include <wincrypt.h>
#include <string>
#include <string_view>
#pragma comment(lib, "crypt32.lib")

namespace DesignNavigation {
// NavigateToString has an about:blank document origin, but recent WebView2
// runtimes report the encoded document URI in NavigationStarting.
inline std::wstring InlineUri(std::wstring_view html)
{
    const int size=WideCharToMultiByte(CP_UTF8,0,html.data(),static_cast<int>(html.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return {};
    std::string utf8(size,'\0');
    if(!WideCharToMultiByte(CP_UTF8,0,html.data(),static_cast<int>(html.size()),utf8.data(),size,nullptr,nullptr))return {};
    DWORD count=0;
    constexpr DWORD flags=CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF;
    const auto bytes=reinterpret_cast<const BYTE*>(utf8.data());
    if(!CryptBinaryToStringW(bytes,size,flags,nullptr,&count))return {};
    std::wstring encoded(count,L'\0');
    if(!CryptBinaryToStringW(bytes,size,flags,encoded.data(),&count))return {};
    encoded.resize(count);
    while(!encoded.empty()&&encoded.back()==L'\0')encoded.pop_back();
    return L"data:text/html;charset=utf-8;base64,"+encoded;
}
inline bool Allow(std::wstring_view uri,std::wstring_view expected,bool pending,bool user,bool redirect)
{
    return pending&&!user&&!redirect&&!expected.empty()&&
        (uri==L"about:blank"||uri==expected);
}
}
