#include "../src/DesignerVisual.h"
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    if(argc==4&&std::wstring_view(argv[1])==L"--standard") {
        const auto html=DesignerVisual::Standard(argv[2],argv[3]);
        if(html.empty())return 1;
        std::cout<<DesignerText::Utf8(html);
        return 0;
    }
    const auto button=DesignerVisual::Standard(L"button",L"1");
    if(button.find(L"background:#27292d")==button.npos||button.find(L"jade.invoke")==button.npos)return 1;
    if(!DesignerVisual::Standard(L"button",L"0").empty())return 1;
    if(!DesignerVisual::Standard(L"unknown",L"1").empty())return 1;
    std::cout<<"Designer templates passed\n";
    return 0;
}
