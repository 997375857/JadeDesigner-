#include "../src/DesignerText.h"
#include "../src/DesignerInspection.h"
#include "../src/ProjectHealth.h"
#include "../src/DiagnosticsInstall.h"
#include "../src/DesignerVisual.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int checks=0;
void check(bool ok,const char* name) { if(!ok) throw std::runtime_error(name); ++checks; }
void put(const std::filesystem::path& path,const std::string& value) {
    std::ofstream f(path,std::ios::binary|std::ios::trunc); f<<value;
}
int main(int argc,char** argv) {
    using namespace DesignerText;
    if(argc==4&&std::string(argv[1])=="--standard") {
        const auto html=DesignerVisual::Standard(Wide(argv[2]),Wide(argv[3]));
        std::cout<<Utf8(html);return html.empty()?2:0;
    }
    const auto root=std::filesystem::absolute(std::filesystem::temp_directory_path()/L"JadeDesignerToolsTests");
    const auto dir=root/(std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(dir);
    const auto cleanup=[&] {
        const auto target=std::filesystem::weakly_canonical(dir);
        if(target.parent_path()!=std::filesystem::weakly_canonical(root)) throw std::runtime_error("unexpected cleanup target");
        std::filesystem::remove_all(target);
    };
    try {
        const std::wstring html=L"\uFEFF<!-- marker --><button id=\"go\"><i></i> 导入 &amp; 标题</button><script>work()</script>";
        auto start=html.find(L" 导入"); auto end=html.find(L"</button>");
        check(TextRange(html,start,end),"plain text");
        check(!TextRange(html,html.find(L"go"),html.find(L"go")+2),"attributes refused");
        check(!TextRange(html,html.find(L"marker"),html.find(L"marker")+6),"comments refused");
        check(!TextRange(html,html.find(L"work"),html.find(L"work")+4),"script refused");
        check(!TextRange(html,start,html.size()+1),"bounds refused");
        check(Wide(Utf8(L"中文\U0001F600"))==L"中文\U0001F600","unicode round trip");
        check(Wide(std::string("\xff",1)).empty(),"invalid UTF8 refused");
        const auto file=dir/L"index.html"; put(file,Utf8(html));
        Document doc; std::string error;
        check(doc.Open(file.wstring(),error),"open");
        check(!doc.Save(doc.revision+1,start,end,html.substr(start,end-start),L"新标题",error),"stale revision");
        check(doc.Save(doc.revision,start,end,html.substr(start,end-start),L"\r\n 新 & <标题>\r\n",error),"save indented label");
        check(doc.bytes.find("&amp; &lt;")!=std::string::npos,"escape markup");
        check(doc.bytes.find("<i></i>")!=std::string::npos,"preserve icon");
        check(doc.bytes.starts_with("\xef\xbb\xbf"),"preserve BOM");
        Sleep(2);
        check(doc.Undo(error)&&doc.bytes==Utf8(html),"undo original bytes");
        check(!doc.Undo(error),"only one undo");
        put(file,"<button>external edit</button>");
        check(!doc.Save(doc.revision,start,end,html.substr(start,end-start),L"new",error),"external change refused");
        check(doc.Open(file.wstring(),error)&&doc.beforeUndo.empty(),"external change resets undo");
        const std::string code=".版本 2\r\n.程序集 类\r\n.子程序 方法, 整数型\r\n.参数 一, 文本型\r\n.参数 二, 整数型\r\n返回 (0)\r\n";
        check(DesignerInspection::Method(code,"方法",2),"method arity");
        check(!DesignerInspection::Method(code,"方法",1),"wrong arity");
        check(!DesignerInspection::Method(code+".子程序 方法\r\n","方法",2),"duplicate method");
        std::string channel,handler;
        check(DesignerInspection::Subscription("JadeView.通讯.订阅 （“ui:test”， &测试_被单击）",channel,handler)&&channel=="ui:test"&&handler=="测试_被单击","fullwidth subscription");
        check(DesignerInspection::Subscription("JadeView.通讯.订阅 (\"ui:test:long\", &测试_被单击_其他)",channel,handler)&&channel!="ui:test"&&handler!="测试_被单击","exact tokens not substrings");
        check(!DesignerInspection::Subscription("' JadeView.通讯.订阅 (\"ui:test\", &测试)",channel,handler),"comment not binding");
        check(!DesignerInspection::Subscription("调试输出 (\"JadeView.通讯.订阅\")",channel,handler),"other command not binding");
        check(SameDocumentUrl(L"file:///E:/%E4%B8%AD%E6%96%87/index.html#part",L"file:///E:/中文/index.html"),"canonical UTF8 file URL");
        check(!SameDocumentUrl(L"https://example.com/index.html",L"file:///E:/中文/index.html"),"remote origin refused");
        check(!SameDocumentUrl(L"file:///E:/other/index.html",L"file:///E:/中文/index.html"),"other project origin refused");
        check(!SameDocumentUrl(L"file:///E:/%2520/index.html",L"file:///E:/%20/index.html"),"no double unescape");
        check(!SameDocumentUrl(L"file:///E:/中文/index.html%00evil",L"file:///E:/中文/index.html"),"no decoded null");
        check(ProjectHealth::DirectCall("Jade_公共_启动 ()\n","Jade_公共_启动"),"direct startup call");
        check(!ProjectHealth::DirectCall("' Jade_公共_启动 ()\n调试输出 (\"Jade_公共_启动 ()\")","Jade_公共_启动"),"comment and literal not startup");
        const auto health=ProjectHealth::Inspect([](const std::string& name,std::string& text,std::string&){
            if(name!="Jade_通讯_订阅集")return 0;
            text=".子程序 Jade_通讯_订阅\nJadeView.通讯.订阅 (\"app:test\", &missing)\nJadeView.通讯.订阅 (\"app:test\", &missing)\n";return 1;
        },[](const std::string&,std::string&,std::string&){return 0;});
        check(std::count_if(health.begin(),health.end(),[](const auto& item){return item.state=="error";})==3,"missing callbacks and duplicate channel");
        const auto unknown=ProjectHealth::Inspect([](const std::string&,std::string&,std::string& why){why="unavailable";return -1;},[](const std::string&,std::string&,std::string&){return -1;});
        check(std::all_of(unknown.begin(),unknown.end(),[](const auto& item){return item.state=="unknown";}),"read failure not absence");
        check(DiagnosticsInstall::HeadEnd(L"<!-- <head> --><html><HEAD lang='x>y'>body")==std::wstring(L"<!-- <head> --><html><HEAD lang='x>y'>").size(),"explicit head scanner");
        check(DiagnosticsInstall::HeadEnd(L"<script>const x='<head>';</script>")==std::wstring::npos,"script text not head");
        check(DiagnosticsInstall::HeadEnd(L"<body>body</body>")==std::wstring::npos,"implicit head refused");
        const auto htmlFile=dir/L"runtime.html";
        const std::string original="<!doctype html><html><head><script src=\"app.js\"></script></head><body>test</body></html>";
        put(htmlFile,original);
        check(DiagnosticsInstall::Install(htmlFile.wstring(),"/*diagnostics*/",error),"runtime install");
        Document installed;check(installed.Open(htmlFile.wstring(),error),"read installed page");
        check(installed.bytes.find("jade-designer-diagnostics-v1.js")<installed.bytes.find("app.js"),"diagnostics before business script");
        check(DiagnosticsInstall::Install(htmlFile.wstring(),"/*diagnostics*/",error),"idempotent install");
        check(!DiagnosticsInstall::Install(htmlFile.wstring(),"/*different*/",error),"existing script not overwritten");
        const std::wstring visualHtml=L"<!doctype html><html><head><script>const x='<button id=\"fake\">';</script></head><body><svg><path d=\"x\"/></svg><button id=\"go\" disabled data-jade-handler=\"原回调\" onclick=\"work()\" style='font-family:\"Test\";'><i></i>原标题</button><section id=\"area\"></section></body></html>";
        const auto visualFile=dir/L"visual.html";put(visualFile,Utf8(visualHtml));Document visual;
        check(visual.Open(visualFile.wstring(),error),"visual open");
        const auto tagStart=visualHtml.find(L"<button id=\"go\""),tagEnd=visualHtml.find(L'>',tagStart)+1;
        const auto oldTag=visualHtml.substr(tagStart,tagEnd-tagStart);
        const auto textStart=visualHtml.find(L"原标题");
        std::vector<DesignerVisual::Edit> edits={{L"style",tagStart,tagEnd,oldTag,L"width",L"180px"},{L"attribute",tagStart,tagEnd,oldTag,L"disabled",L"0"},{L"text",textStart,textStart+3,L"原标题",L"",L"新 <标题>"}};
        check(DesignerVisual::Save(visual,visual.revision,edits,error),"visual atomic title and properties");
        check(visual.bytes.find("data-jade-handler=\"原回调\" onclick=\"work()\"")!=std::string::npos,"visual preserves handler and script");
        check(visual.bytes.find("font-family:&quot;Test&quot;;")!=std::string::npos,"visual preserves quoted author CSS");
        check(visual.bytes.find("width:180px !important;")!=std::string::npos&&visual.bytes.find("新 &lt;标题&gt;")!=std::string::npos,"visual typed style and escaped title");
        check(visual.Undo(error)&&visual.bytes==Utf8(visualHtml),"visual batch undo");
        const auto originalEdits=edits;
        edits={{L"style",tagStart,tagEnd,oldTag,L"translate",L"-12px 24px"}};
        check(DesignerVisual::Save(visual,visual.revision,edits,error)&&visual.bytes.find("translate:-12px 24px !important;")!=std::string::npos,"signed pixel position saved");
        check(visual.bytes.find("onclick=\"work()\"")!=std::string::npos&&visual.bytes.find("font-family:&quot;Test&quot;;")!=std::string::npos,"position preserves listeners and author styling");
        check(visual.Undo(error)&&visual.bytes==Utf8(visualHtml),"position undo restores exact source");
        for(const auto value:{L"2001px 0px",L"0px -2001px",L"1px 2px 3px",L"1px 2px;display:none",L"calc(1px) 2px",L"1.5px 0px",L" 0px",L"0px "}) {
            edits[0].value=value;
            check(!DesignerVisual::Save(visual,visual.revision,edits,error)&&visual.bytes==Utf8(visualHtml),"invalid position refused without writing");
        }
        edits=originalEdits;
        edits[0].value=L"10px;background:url(x)";
        check(!DesignerVisual::Save(visual,visual.revision,edits,error)&&visual.bytes==Utf8(visualHtml),"arbitrary CSS refused atomically");
        edits={{L"attribute",tagStart,tagEnd,oldTag,L"id",L"other"}};
        check(!DesignerVisual::Save(visual,visual.revision,edits,error),"identity edits refused");
        edits={{L"attribute",tagStart,tagEnd,oldTag,L"onclick",L"evil()"}};
        check(!DesignerVisual::Save(visual,visual.revision,edits,error),"new script property refused");
        const auto insertion=visualHtml.find(L"</section>");
        edits={{L"insert",insertion,insertion+10,L"</section>",L"button",L"1"}};
        check(DesignerVisual::Save(visual,visual.revision,edits,error),"standard control insertion");
        check(visual.bytes.find("界面按钮1_被单击")!=std::string::npos&&visual.bytes.find("jade.invoke('ui:jade_button_1'")!=std::string::npos,"new control includes runtime wiring and Chinese name");
        check(visual.Undo(error),"insert undo");
        edits[0].key=L"script";check(!DesignerVisual::Save(visual,visual.revision,edits,error),"unknown palette item refused");
        DesignerVisual::Tag token;std::vector<std::wstring> parents;
        const std::wstring templateHtml=L"<html><body><template><button>x</button></template></body></html>";
        check(!DesignerVisual::At(templateHtml,templateHtml.find(L"<button"),token,parents),"template target refused");
        check(DesignerVisual::ParseTag(L"<input disabled checked id='x'>",0,token)&&token.attrs.size()==3,"boolean attributes before other attributes");
        check(!DesignerVisual::ParseTag(L"<button id='x' id='y'>",0,token),"duplicate attributes refused");
        put(visualFile,"<html><body>external</body></html>");edits={{L"style",tagStart,tagEnd,oldTag,L"width",L"120px"}};
        check(!DesignerVisual::Save(visual,visual.revision,edits,error),"visual refuses external edits");
        cleanup();
        std::cout<<checks<<" designer tool checks passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<"\n";
        cleanup();
        return 1;
    }
}
