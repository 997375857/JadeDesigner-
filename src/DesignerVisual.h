#pragma once
#include "DesignerText.h"
#include <map>
#include <set>

namespace DesignerVisual {
struct Attribute { std::wstring name; size_t start=0,end=0,valueStart=0,valueEnd=0; };
struct Tag { std::wstring name; size_t start=0,end=0; bool closing=false,self=false; std::vector<Attribute> attrs; };
inline std::wstring Lower(std::wstring s) { for(auto& c:s)c=static_cast<wchar_t>(towlower(c));return s; }
inline bool Space(wchar_t c) { return c==L' '||c==L'\t'||c==L'\r'||c==L'\n'||c==L'\f'; }
inline bool Name(wchar_t c) { return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'-'||c==L'_'||c==L':'; }
inline bool ParseTag(std::wstring_view source,size_t start,Tag& tag) {
    if(start>=source.size()||source[start]!=L'<')return false;
    tag={};tag.start=start;size_t p=start+1;
    if(p<source.size()&&source[p]==L'/'){tag.closing=true;++p;}
    const auto n=p;while(p<source.size()&&Name(source[p]))++p;
    if(n==p)return false;tag.name=Lower(std::wstring(source.substr(n,p-n)));
    std::set<std::wstring> names;
    while(p<source.size()) {
        const auto whitespace=p;while(p<source.size()&&Space(source[p]))++p;
        if(p>=source.size())return false;
        if(source[p]==L'>'){tag.end=p+1;return true;}
        if(source[p]==L'/'&&p+1<source.size()&&source[p+1]==L'>'){tag.self=true;tag.end=p+2;return true;}
        if(tag.closing||p==whitespace)return false;
        Attribute a;a.start=whitespace;const auto begin=p;
        while(p<source.size()&&Name(source[p]))++p;
        if(p==begin)return false;a.name=Lower(std::wstring(source.substr(begin,p-begin)));
        if(!names.insert(a.name).second)return false;
        const auto nameEnd=p;while(p<source.size()&&Space(source[p]))++p;
        a.valueStart=a.valueEnd=p;
        if(p<source.size()&&source[p]==L'=') {
            ++p;while(p<source.size()&&Space(source[p]))++p;
            if(p>=source.size())return false;
            const auto quote=source[p];
            if(quote==L'\''||quote==L'"') {
                a.valueStart=++p;while(p<source.size()&&source[p]!=quote)++p;
                if(p>=source.size())return false;a.valueEnd=p++;
            } else {
                a.valueStart=p;while(p<source.size()&&!Space(source[p])&&source[p]!=L'>') {
                    if(source[p]==L'<'||source[p]==L'`'||source[p]==L'"'||source[p]==L'\'')return false;++p;
                }
                a.valueEnd=p;if(p==a.valueStart)return false;
            }
        } else p=nameEnd;
        a.end=p;tag.attrs.push_back(a);
    }
    return false;
}
inline bool Void(const std::wstring& tag) { return std::set<std::wstring>{L"area",L"base",L"br",L"col",L"embed",L"hr",L"img",L"input",L"link",L"meta",L"param",L"source",L"track",L"wbr"}.contains(tag); }
inline bool Raw(const std::wstring& tag) { return std::set<std::wstring>{L"script",L"style",L"textarea",L"title",L"iframe",L"xmp",L"noscript"}.contains(tag); }
inline bool Blocked(const std::wstring& tag) { return Raw(tag)||tag==L"template"||tag==L"svg"||tag==L"math"; }
// Conservative guard, not a second HTML5 parser. Implicit/malformed nesting is
// refused; parse5 supplies the exact locations on the renderer side.
inline bool At(std::wstring_view source,size_t offset,Tag& target,std::vector<std::wstring>& stack) {
    stack.clear();const auto lower=Lower(std::wstring(source));
    for(size_t p=0;p<source.size();) {
        if(source[p]!=L'<'){++p;continue;}
        if(source.substr(p,4)==L"<!--") {auto q=source.find(L"-->",p+4);if(q==source.npos)return false;p=q+3;continue;}
        if(lower.substr(p,9)==L"<!doctype") {auto q=source.find(L'>',p+9);if(q==source.npos)return false;p=q+1;continue;}
        Tag tag;if(!ParseTag(source,p,tag))return false;
        if(p==offset) {target=tag;return std::none_of(stack.begin(),stack.end(),Blocked);}
        if(p>offset)return false;
        if(tag.closing) {if(stack.empty()||stack.back()!=tag.name)return false;stack.pop_back();}
        else if(!Void(tag.name)) {
            if(tag.self) {if(std::find(stack.begin(),stack.end(),L"svg")!=stack.end()||std::find(stack.begin(),stack.end(),L"math")!=stack.end()){p=tag.end;continue;}return false;}
            stack.push_back(tag.name);
            if(Raw(tag.name)) {
                auto q=lower.find(L"</"+tag.name,tag.end);if(q==source.npos||offset<q)return false;
                p=q;continue;
            }
        }
        p=tag.end;
    }
    return false;
}
inline bool Control(const std::wstring& tag) { return std::set<std::wstring>{L"body",L"div",L"section",L"main",L"article",L"aside",L"header",L"footer",L"nav",L"form",L"button",L"label",L"span",L"p",L"h1",L"h2",L"h3",L"h4",L"h5",L"h6",L"input",L"select",L"textarea",L"option",L"progress",L"img"}.contains(tag); }
inline bool Container(const std::wstring& tag) {return std::set<std::wstring>{L"body",L"div",L"section",L"main",L"article",L"aside",L"header",L"footer",L"nav",L"form"}.contains(tag);}
inline std::wstring EscapeAttribute(std::wstring_view text) {
    std::wstring out;for(const auto c:text) {
        if(c==L'&')out+=L"&amp;";else if(c==L'"')out+=L"&quot;";else if(c==L'<')out+=L"&lt;";else if(c==L'>')out+=L"&gt;";else out+=c;
    }return out;
}
inline std::wstring SetAttribute(std::wstring text,const std::wstring& key,const std::wstring& encoded,bool remove=false) {
    Tag tag;if(!ParseTag(text,0,tag))return {};
    const auto replacement=remove?L"":L" "+key+L"=\""+encoded+L"\"";
    for(const auto& a:tag.attrs)if(a.name==key){text.replace(a.start,a.end-a.start,replacement);return text;}
    if(!remove)text.insert(text.size()-(tag.self?2:1),replacement);
    return text;
}
inline bool CssValue(const std::wstring& key,const std::wstring& value) {
    if(key==L"translate") {
        const auto split=value.find(L' ');if(split==value.npos||value.find(L' ',split+1)!=value.npos)return false;
        const auto pixel=[](const std::wstring& s) {
            if(!s.ends_with(L"px")||s.size()<3||s.size()>7)return false;
            const auto digits=s.substr(0,s.size()-2);const size_t start=digits[0]==L'-'?1:0;
            if(start==digits.size()||digits.find_first_not_of(L"0123456789",start)!=digits.npos)return false;
            const int number=std::stoi(digits);return number>=-2000&&number<=2000;
        };
        return pixel(value.substr(0,split))&&pixel(value.substr(split+1));
    }
    if(key==L"color"||key==L"background-color")return value==L"inherit"||(value.size()==7&&value[0]==L'#'&&value.find_first_not_of(L"0123456789abcdefABCDEF",1)==value.npos);
    if(key==L"visibility")return value==L"hidden"||value==L"visible";
    if(key==L"display")return value==L"flex"||value==L"block";
    if(key==L"flex-direction")return value==L"row"||value==L"column";
    if(key==L"align-items")return value==L"stretch"||value==L"center"||value==L"flex-start"||value==L"flex-end";
    if(key==L"justify-content")return value==L"flex-start"||value==L"center"||value==L"flex-end"||value==L"space-between";
    if(key==L"position")return value==L"static"||value==L"relative"||value==L"absolute"||value==L"fixed"||value==L"sticky";
    if(key==L"left"||key==L"top") {
        if(!value.ends_with(L"px")||value.size()<3||value.size()>9)return false;
        const auto digits=value.substr(0,value.size()-2); const size_t start=digits[0]==L'-'?1:0;
        if(start==digits.size()||digits.find_first_not_of(L"0123456789",start)!=digits.npos)return false;
        const auto n=std::stol(digits); return n>=-4000&&n<=4000;
    }
    if(!std::set<std::wstring>{L"width",L"height",L"font-size",L"border-radius",L"padding",L"gap"}.contains(key))return false;
    if((key==L"width"||key==L"height")&&(value==L"auto"||value==L"100%"))return true;
    if(!value.ends_with(L"px")||value.size()<3||value.size()>6)return false;
    const auto digits=value.substr(0,value.size()-2);if(digits.find_first_not_of(L"0123456789")!=digits.npos)return false;
    const auto n=std::stoul(digits);return n<=4000&&(key!=L"font-size"||(n>=8&&n<=160));
}
inline std::wstring RoutineName(const std::wstring& kind,const std::wstring& number) {
    if(kind==L"button")return L"界面按钮"+number+L"_被单击";
    if(kind==L"checkbox")return L"界面复选框"+number+L"_选中状态被改变";
    if(kind==L"radio")return L"界面单选框"+number+L"_选中状态被改变";
    if(kind==L"select")return L"界面选择框"+number+L"_选择项被改变";
    return {};
}
inline std::wstring Standard(const std::wstring& kind,const std::wstring& number) {
    if(number.empty()||number.size()>6||number.find_first_not_of(L"0123456789")!=number.npos||number[0]==L'0')return {};
    const auto id=L"jade_"+kind+L"_"+number;
    const auto attr=L" id=\""+id+L"\"";
    const auto event=[&]() {
        const auto dom=kind==L"button"?L"onclick":L"onchange";
        return L" data-jade-handler=\""+RoutineName(kind,number)+L"\" data-jade-channel=\"ui:"+id+L"\" "+dom+
            L"=\"if(window.jade){jade.invoke('ui:"+id+L"',{value:this.value,checked:this.checked===true}).catch(function(){console.error('Jade request failed');});}else{console.warn('JadeView unavailable');}\"";
    };
    const auto base=L" style=\"box-sizing:border-box;max-width:100%;font:inherit;\"";
    if(kind==L"button")return L"<button type=\"button\""+attr+event()+L" style=\"box-sizing:border-box;max-width:100%;font:inherit;font-weight:600;padding:8px 16px;min-height:38px;border:1px solid #27292d;border-radius:6px;background:#27292d;color:#fff;box-shadow:0 4px 10px #00000020;\">按钮"+number+L"</button>";
    if(kind==L"label")return L"<span"+attr+base+L">标签"+number+L"</span>";
    if(kind==L"heading")return L"<h2"+attr+L">标题"+number+L"</h2>";
    if(kind==L"input")return L"<input type=\"text\""+attr+base+L" placeholder=\"请输入内容\" aria-label=\"输入框"+number+L"\">";
    if(kind==L"textarea")return L"<textarea"+attr+base+L" rows=\"3\" placeholder=\"请输入内容\" aria-label=\"多行输入框"+number+L"\"></textarea>";
    if(kind==L"checkbox"||kind==L"radio") {
        const auto name=kind==L"checkbox"?L"复选框":L"单选框";
        return L"<label><input type=\""+kind+L"\""+attr+event()+L" name=\""+(kind==L"radio"?L"jade_radio_group":id)+L"\">"+name+number+L"</label>";
    }
    if(kind==L"select")return L"<select"+attr+base+event()+L" aria-label=\"选择框"+number+L"\"><option value=\"1\">选项一</option><option value=\"2\">选项二</option><option value=\"3\">选项三</option></select>";
    if(kind==L"container")return L"<div"+attr+L" style=\"display:flex;flex-direction:column;gap:12px;padding:12px;min-height:60px;box-sizing:border-box;border:1px solid #e5e5ea;border-radius:6px;\"></div>";
    if(kind==L"progress")return L"<progress"+attr+L" value=\"0\" max=\"100\" aria-label=\"进度条"+number+L"\"></progress>";
    return {};
}
struct Edit {std::wstring kind;size_t start=0,end=0;std::wstring old,key,value;};
inline bool Save(DesignerText::Document& document,unsigned revision,const std::vector<Edit>& edits,std::string& error) {
    const auto source=DesignerText::Wide(document.bytes);
    if(revision!=document.revision||edits.empty()||edits.size()>32){error="编辑已过期或修改数量异常，请重新选择控件";return false;}
    struct Patch{size_t end;std::wstring next;};std::map<size_t,Patch> patches;
    std::map<size_t,std::pair<Tag,std::vector<std::wstring>>> contexts;
    for(const auto& e:edits) {
        if(e.start>=e.end||e.end>source.size()||source.substr(e.start,e.end-e.start)!=e.old||e.value.size()>8192||
            std::any_of(e.value.begin(),e.value.end(),[](wchar_t c){return c<32&&c!=L'\n'&&c!=L'\r'&&c!=L'\t';})) {error="源文件位置或控件已变化，未写入";return false;}
        Tag tag;std::vector<std::wstring> stack;
        if(e.kind==L"remove") {
            if(!At(source,e.start,tag,stack)||tag.name==L"body"||Blocked(tag.name)||e.end<=tag.end||source[e.end-1]!=L'>') {error="控件源码范围不明确，未删除";return false;}
            patches[e.start]={e.end,L""};continue;
        }
        if(e.kind==L"text") {
            if(!DesignerText::TextRange(source,e.start,e.end)||patches.contains(e.start)){error="不是可编辑的静态文字";return false;}
            patches[e.start]={e.end,DesignerText::Escape(e.value)};continue;
        }
        if(const auto cached=contexts.find(e.start);cached!=contexts.end()){tag=cached->second.first;stack=cached->second.second;}
        else {if(!At(source,e.start,tag,stack)){error="HTML 结构不明确，未修改";return false;}contexts[e.start]={tag,stack};}
        if(tag.end!=e.end){error="HTML 结构不明确，未修改";return false;}
        if(e.kind==L"insert") {
            if(!tag.closing||stack.empty()||stack.back()!=tag.name||!Container(tag.name)||
                std::any_of(stack.begin(),stack.end(),[](const auto& t){return t==L"button"||t==L"a"||t==L"select"||t==L"table";})||patches.contains(e.start)){error="只能在明确的布局容器末尾添加控件";return false;}
            const auto html=Standard(e.key,e.value);const auto id=L"jade_"+e.key+L"_"+e.value;
            if(html.empty()||source.find(id)!=source.npos){error="控件类型无效或标识已经存在";return false;}
            patches[e.start]={e.end,L"\n"+html+L"\n"+e.old};continue;
        }
        if(tag.closing||!Control(tag.name)||tag.name==L"body"){error="该元素不支持属性编辑";return false;}
        auto [it,inserted]=patches.try_emplace(e.start,Patch{e.end,e.old});(void)inserted;
        if(it->second.end!=e.end){error="修改范围重叠";return false;}
        auto& next=it->second.next;
        if(e.kind==L"style") {
            if(!CssValue(e.key,e.value)){error="样式只接受受支持的尺寸、颜色和布局选项";return false;}
            Tag updated;if(!ParseTag(next,0,updated)){error="属性解析失败";return false;}
            std::wstring style;
            for(const auto& a:updated.attrs)if(a.name==L"style")style=next.substr(a.valueStart,a.valueEnd-a.valueStart);
            std::wstring quoted;for(const auto c:style){if(c==L'"')quoted+=L"&quot;";else if(c==L'<')quoted+=L"&lt;";else if(c==L'>')quoted+=L"&gt;";else quoted+=c;}
            // Preserve the author's declarations and append only validated values.
            // Never accept URL(), arbitrary CSS or event attributes from the page.
            next=SetAttribute(next,L"style",quoted+L";"+e.key+L":"+e.value+L" !important;");
        } else if(e.kind==L"attribute") {
            const bool boolean=e.key==L"disabled"||e.key==L"readonly"||e.key==L"checked"||e.key==L"selected";
            const bool allowed=e.key==L"title"||((e.key==L"placeholder"||e.key==L"readonly")&&(tag.name==L"input"||tag.name==L"textarea"))||
                (e.key==L"value"&&(tag.name==L"input"||tag.name==L"option"||tag.name==L"progress"))||
                (e.key==L"disabled"&&(tag.name==L"button"||tag.name==L"input"||tag.name==L"select"||tag.name==L"textarea"))||
                (e.key==L"checked"&&tag.name==L"input")||(e.key==L"selected"&&tag.name==L"option");
            if(!allowed||(boolean&&e.value!=L"0"&&e.value!=L"1")){error="不允许修改标识、频道、回调或脚本属性";return false;}
            next=SetAttribute(next,e.key,boolean?L"":EscapeAttribute(e.value),boolean&&e.value==L"0");
        } else {error="未知的属性操作";return false;}
        if(next.empty()){error="属性解析失败";return false;}
    }
    size_t end=0;for(const auto& [start,patch]:patches){if(start<end){error="修改范围重叠";return false;}end=patch.end;}
    auto next=source;for(auto i=patches.rbegin();i!=patches.rend();++i)next.replace(i->first,i->second.end-i->first,i->second.next);
    const auto encoded=DesignerText::Utf8(next);
    if(encoded.empty()||encoded.size()>2*1024*1024){error="修改超过 HTML 大小限制或包含无效字符";return false;}
    return document.WriteChecked(encoded,error);
}
}
