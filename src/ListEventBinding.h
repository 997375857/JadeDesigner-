#pragma once
#include "DesignerInspection.h"
#include <cstdint>
#include <functional>
#include <iomanip>

// Ordinary assemblies own persistent objects. No extra factory class or DLL
// declarations are emitted into a user's project.
namespace ListEventBinding {
inline constexpr auto SubscriptionAssembly = "Jade_通讯_订阅集";
inline constexpr auto SubscriptionRoutine = "Jade_通讯_订阅";
using Reader = std::function<int(const std::string&, std::string&, std::string&)>;

inline std::string Compact(std::string text) {
    for (const auto& [from,to] : std::vector<std::pair<std::string,std::string>>{
        {"（","("},{"）",")"},{"，",","},{"“","\""},{"”","\""},{"＝","="},{"．","."}}) {
        size_t pos=0;
        while ((pos=text.find(from,pos))!=text.npos) {text.replace(pos,from.size(),to);pos+=to.size();}
    }
    std::string out; bool quoted=false;
    for (char c:text) {
        if (c=='"') quoted=!quoted;
        if (c=='\''&&!quoted) break;
        if (quoted || (c!=' '&&c!='\t'&&c!='\r')) out+=!quoted&&c>='A'&&c<='Z'?char(c-'A'+'a'):c;
    }
    return out;
}
inline int LineCount(const std::string& source,const std::string& statement) {
    std::istringstream lines(source); std::string line; int count=0;
    while(std::getline(lines,line)) if(Compact(line)==Compact(statement)) ++count;
    return count;
}
inline std::string RoutineSource(const std::string& source,const std::string& name) {
    for(const auto& routine:DesignerInspection::Routines(source))
        if(_stricmp(routine.name.c_str(),name.c_str())==0) return routine.source;
    return {};
}
inline bool SafeId(const std::string& id) {
    if(id.empty()||id.size()>512) return false;
    for(unsigned char c:id) if(c<32||c==127||c=='"'||c=='\'') return false;
    return id.find("“")==id.npos && id.find("”")==id.npos;
}
inline std::string Token(const std::string& id) {
    uint64_t hash=14695981039346656037ull;
    for(unsigned char c:id){hash^=c;hash*=1099511628211ull;}
    std::ostringstream out; out<<std::hex<<std::setfill('0')<<std::setw(16)<<hash;
    return out.str();
}
inline std::string ReadableStem(const std::string& id) {
    std::string out;size_t letters=0;
    for(size_t i=0;i<id.size()&&letters<24;++letters) {
        const auto c=static_cast<unsigned char>(id[i]);
        if(c<128) {out+=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')?char(c):'_';++i;}
        else {const size_t size=c>=240?4:c>=224?3:2;if(i+size>id.size())break;out+=id.substr(i,size);i+=size;}
    }
    return out;
}
struct Plan {
    std::string id,event,label,handler,callbackAssembly,assembly,object,initialize,marker,eventFlag;
    std::string assignment,registration,guardOpen,guardSet,guardClose,startup,initializer,source,callback;
    bool existingBinding=false;
};

struct ExistingBinding {
    bool found=false;
    bool ambiguous=false;
    std::string object, routine, assignment;
};

inline std::string HandlerName(const std::string& object,const std::string& label) {
    return object+label;
}

inline std::string CallbackSource(const std::string& handler) {
    return ".子程序 "+handler+", 整数型\r\n.参数 WinId, 整数型\r\n.参数 msg, 文本型\r\n\r\n"+
        "msg ＝ UTF8文本到GBK文本 (msg)\r\n返回 (JadeView.文本.创建指针 (\"ok\"))\r\n";
}

inline std::string Trim(std::string text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==text.npos)return {};
    const auto last=text.find_last_not_of(" \t\r\n");
    return text.substr(first,last-first+1);
}

// Locate the variable assignment the user already wrote. This intentionally
// extracts the variable from the original line instead of Compact(), which
// lowercases ASCII identifiers and would make generated Easy Language names
// case-sensitive in the editor.
inline ExistingBinding FindExistingBinding(const std::string& source,const std::string& id) {
    ExistingBinding out;
    const std::string needle="=jade超级列表框绑定(\""+id+"\"";
    for(const auto& routine:DesignerInspection::Routines(source)) {
        std::istringstream lines(routine.source);std::string line;
        while(std::getline(lines,line)) {
            std::string normalized=line;
            for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{{"＝","="},{"（","("},{"）",")"},{"，",","},{"“","\""},{"”","\""}}) {
                size_t pos=0;while((pos=normalized.find(from,pos))!=normalized.npos){normalized.replace(pos,from.size(),to);pos+=to.size();}
            }
            if(Compact(normalized).find(Compact(needle))==std::string::npos)continue;
            const auto equals=normalized.find('=');
            if(equals==normalized.npos)continue;
            const auto object=Trim(normalized.substr(0,equals));
            // Fullwidth punctuation was normalized above. Keep this byte set
            // ASCII-only so Chinese identifier bytes cannot match by accident.
            if(object.empty()||object.find_first_of(" \t.(),\"")!=object.npos)continue;
            if(out.found&&(_stricmp(out.object.c_str(),object.c_str())!=0||
                _stricmp(out.routine.c_str(),routine.name.c_str())!=0)) {
                out.ambiguous=true;
                continue;
            }
            out.found=true;out.object=object;out.routine=routine.name;out.assignment=Trim(normalized);
        }
    }
    return out;
}
inline Plan Make(const std::string& id,const std::string& event,const std::string& label,
    const std::string& handler,const std::string& callbackAssembly) {
    Plan p; p.id=id;p.event=event;p.label=label;
    const auto suffix=ReadableStem(id)+"_"+Token(id);
    p.assembly="Jade_列表_"+suffix;p.object="超级列表框_"+suffix;
    p.initialize="Jade_列表绑定_"+suffix;p.marker.clear();
    // Keep the user callback with ordinary JadeView subscriptions. The
    // per-list assembly only owns the persistent object and bind state.
    p.handler=handler;p.callbackAssembly=callbackAssembly.empty()?std::string(SubscriptionAssembly):callbackAssembly;
    p.assignment=p.object+" ＝ Jade超级列表框绑定 (\""+id+"\")";
    p.eventFlag="Jade_列表事件_"+event+"_已绑定";
    p.guardOpen=".如果真 ("+p.eventFlag+" ＝ 假)";
    p.registration=p.object+".绑定事件 (\""+label+"\", &"+handler+")";
    p.guardSet=p.eventFlag+" ＝ 真";
    p.guardClose=".如果真结束";
    p.startup=p.initialize+" ()";
    p.initializer=".子程序 "+p.initialize+"\n.如果真 (Jade_列表已绑定 ＝ 假)\n    "+p.assignment+
        "\n    Jade_列表已绑定 ＝ 真\n.如果真结束\n"+p.guardOpen+"\n    "+p.registration+
        "\n    "+p.guardSet+"\n"+p.guardClose+"\n";
    p.source=".版本 2\n.程序集 "+p.assembly+"\n.程序集变量 "+p.object+
        ", JadeView超级列表框对象\n.程序集变量 Jade_列表已绑定, 逻辑型\n";
    for(const auto& code:std::vector<std::string>{"click","dblclick","select","checkbox-change","radio-change","contextmenu","mouseenter","mouseleave","scroll-bottom"})
        p.source += ".程序集变量 Jade_列表事件_"+code+"_已绑定, 逻辑型\n";
    p.source += "\n"+p.initializer;
    // Use the native parser's CRLF format, as the ordinary subscription path does.
    p.callback=CallbackSource(handler);
    return p;
}
struct Inspection {
    std::string status="unknown",message;
    bool hasAssembly=false,eventComplete=false,startupComplete=false,eventStatementPresent=false;
};
inline Inspection Inspect(const Plan& p,const Reader& readAssembly,const Reader& readRoutine) {
    Inspection out;
    const auto conflict=[&](const std::string& message){out.status="conflict";out.message=message;return out;};
    if(!SafeId(p.id)) return conflict("控件 ID 包含易语言文本常量不支持的字符，未写入工程");
    std::string source,subscriptions,callback,error;
    const int found=readAssembly(p.assembly,source,error);
    const int subs=readAssembly(SubscriptionAssembly,subscriptions,error);
    const int cb=p.callbackAssembly==p.assembly?found:readAssembly(p.callbackAssembly,callback,error);
    if(p.callbackAssembly==p.assembly) callback=source;
    if(found<0||subs<0||cb<0){out.message="内存读取未完成："+error;return out;}
    out.hasAssembly=found==1;
    const auto subroutines=DesignerInspection::Routines(subscriptions);
    const auto callbacks=DesignerInspection::Routines(callback);
    const int count=DesignerInspection::Count(callbacks,p.handler);
    int registrations=0;
    if(count>1||DesignerInspection::Count(subroutines,SubscriptionRoutine)>1)
        return conflict("同名回调或订阅入口重复，未自动修改");
    if(DesignerInspection::Count(subroutines,SubscriptionRoutine)==1 &&
        !DesignerInspection::Method(subscriptions,SubscriptionRoutine,0))
        return conflict("现有 Jade_通讯_订阅 入口需要参数，不能安全加入自动绑定");
    const auto init=RoutineSource(source,p.initialize);
    if(found==1) {
        if(!DesignerInspection::Method(source,p.initialize,0) ||
            LineCount(source,".程序集变量 "+p.object+", JadeView超级列表框对象")!=1 ||
            LineCount(source,".程序集变量 Jade_列表已绑定, 逻辑型")!=1 ||
            LineCount(source,".程序集变量 "+p.eventFlag+", 逻辑型")!=1)
            return conflict("列表绑定程序集与生成结构不一致，请人工核对；不会覆盖原代码");
        if(LineCount(init,p.assignment)!=1 || LineCount(init,".如果真 (Jade_列表已绑定 ＝ 假)")!=1 ||
            LineCount(init,"Jade_列表已绑定 ＝ 真")!=1 || LineCount(init,".如果真结束")!=2)
            return conflict("列表初始化逻辑已修改，请人工核对");
        std::istringstream lines(init);std::string line;
        while(std::getline(lines,line)) {
            const auto compact=Compact(line);
            if(compact.starts_with("返回(") || compact.starts_with(".返回"))
                return conflict("列表初始化入口包含提前返回，不能安全追加事件");
            const auto prefix=Compact(p.object+".绑定事件");
            if(compact.starts_with(prefix)) {
                const bool targetHandler=compact.find("&"+Compact(p.handler))!=compact.npos;
                const bool targetLabel=compact.find("\""+Compact(p.label)+"\"")!=compact.npos ||
                    (p.label=="被单击"&&compact.find("\"单击\"")!=compact.npos) ||
                    (p.label=="被双击"&&compact.find("\"双击\"")!=compact.npos);
                if(targetLabel&&targetHandler)++registrations;
                else if(targetLabel&&!targetHandler)return conflict("该列表事件已经绑定其他回调，未重复订阅");
            }
        }
    }
    // Check the global callback namespace before creating any assembly.
    std::string global;
    const int globalCount=readRoutine(p.handler,global,error);
    if(globalCount<0){out.message="回调名称无法核验："+error;return out;}
    if(globalCount==1&&count==0) return conflict("其他程序集已使用该回调名称，未重复创建");
    if(count==1) {
        const auto routine=RoutineSource(callback,p.handler);
        const auto parsed=DesignerInspection::Routines(routine);
        const auto header=Compact(routine.substr(0,routine.find('\n')));
        const auto headerComma=header.find(','),next=headerComma==header.npos?header.npos:header.find(',',headerComma+1);
        const auto returnType=headerComma==header.npos?std::string():header.substr(headerComma+1,next==header.npos?next:next-headerComma-1);
        if(parsed.empty()||parsed.front().parameters!=2||
            returnType!="整数型")
            return conflict("现有回调须返回整数型并接收 WinId、msg 两个参数，请核对签名");
        std::istringstream declarations(routine);std::string declaration;size_t index=0;
        for(const auto& type:std::vector<std::string>{"整数型","文本型"}) {
            bool valid=false;
            while(std::getline(declarations,declaration)) {
                const auto compact=Compact(declaration);
                if(!compact.starts_with(".参数"))continue;
                const auto comma=compact.find(',');
                if(comma!=compact.npos) {
                    const auto end=compact.find(',',comma+1);
                    valid=compact.substr(comma+1,end==compact.npos?end:end-comma-1)==type;
                    // Reference/array parameters have a different callback ABI.
                    if(end!=compact.npos) {
                        const auto afterFlags=compact.find(',',end+1);
                        const auto flags=compact.substr(end+1,afterFlags==compact.npos?afterFlags:afterFlags-end-1);
                        valid=valid&&flags.find("传址")==flags.npos&&flags.find("参考")==flags.npos&&flags.find("数组")==flags.npos;
                    }
                }
                break;
            }
            if(!valid)return conflict("回调第 "+std::to_string(++index)+" 个参数类型不匹配，未写入代码");
            ++index;
        }
    }
    const auto startup=RoutineSource(subscriptions,SubscriptionRoutine);
    std::istringstream lines(startup);std::string line;
    while(std::getline(lines,line)) {
        if(Compact(line).starts_with("返回("))return conflict("订阅入口包含提前返回，请人工确认列表绑定的调用位置");
        std::string channel,target;
        if(DesignerInspection::Subscription(line,channel,target)&&channel=="jade:list:event:"+p.id+":"+p.event)
            return conflict("存在该事件的旧式直接订阅，请先合并旧订阅，避免重复回调");
    }
    const int starts=LineCount(startup,p.startup);
    if(registrations>1||starts>1) return conflict("列表事件或启动调用重复，未自动修改");
    out.eventStatementPresent=registrations==1;
    const bool guardComplete=LineCount(init,p.guardOpen)==1&&LineCount(init,p.guardSet)==1&&LineCount(init,p.guardClose)>=2;
    out.eventComplete=count==1&&out.eventStatementPresent&&guardComplete;
    out.startupComplete=starts==1;
    out.status=out.hasAssembly&&count==1&&registrations==1&&starts==1?"complete":"missing";
    out.message=out.status=="complete"?"列表对象、事件回调和启动绑定均已存在":"需补齐列表对象、事件回调或启动绑定";
    return out;
}

// Inspection for the preferred mode: the user owns the binding assignment in
// an ordinary assembly, and the generator only adds the missing object event
// statement plus the callback routine in the shared subscription assembly.
inline Inspection InspectExisting(const Plan& p,const Reader& readAssembly,const Reader& readRoutine) {
    Inspection out;
    const auto conflict=[&](const std::string& message){out.status="conflict";out.message=message;return out;};
    if(!p.existingBinding||p.assembly.empty()||p.object.empty()||p.initialize.empty())
        return conflict("未找到易语言中的超级列表框绑定，请先写入 Jade超级列表框绑定 (控件ID)");
    std::string source,callback,error;
    const int found=readAssembly(p.assembly,source,error);
    const int callbacks=readAssembly(p.callbackAssembly,callback,error);
    if(found<0||callbacks<0){out.message="内存读取未完成："+error;return out;}
    if(found!=1)return conflict("已找到绑定记录，但绑定程序集无法读取，未写入工程");
    const auto init=RoutineSource(source,p.initialize);
    if(init.empty())return conflict("已找到绑定语句，但绑定子程序无法读取，未写入工程");
    out.hasAssembly=true;
    int registrations=0;
    std::istringstream lines(init);std::string line;
    const auto prefix=Compact(p.object+".绑定事件");
    while(std::getline(lines,line)) {
        const auto compact=Compact(line);
        if(!compact.starts_with(prefix))continue;
        const bool targetHandler=compact.find("&"+Compact(p.handler))!=compact.npos;
        const bool targetLabel=compact.find("\""+Compact(p.label)+"\"")!=compact.npos ||
            (p.label=="被单击"&&compact.find("\"单击\"")!=compact.npos) ||
            (p.label=="被双击"&&compact.find("\"双击\"")!=compact.npos);
        if(targetLabel&&targetHandler)++registrations;
        else if(targetLabel)return conflict("该列表事件已经绑定其他回调，未重复写入");
    }
    std::string global;
    const int globalCount=readRoutine(p.handler,global,error);
    if(globalCount<0){out.message="回调名称无法核验："+error;return out;}
    const auto callbackRoutines=DesignerInspection::Routines(callback);
    const int callbackCount=DesignerInspection::Count(callbackRoutines,p.handler);
    if(callbackCount>1||globalCount>1)return conflict("同名回调重复，未自动修改");
    if(globalCount==1&&callbackCount==0)return conflict("其他程序集已使用该回调名称，未重复创建");
    if(callbackCount==1) {
        const auto routine=RoutineSource(callback,p.handler);
        const auto parsed=DesignerInspection::Routines(routine);
        const auto header=Compact(routine.substr(0,routine.find('\n')));
        const auto comma=header.find(','),next=comma==header.npos?header.npos:header.find(',',comma+1);
        const auto returnType=comma==header.npos?std::string():header.substr(comma+1,next==header.npos?next:next-comma-1);
        if(parsed.empty()||parsed.front().parameters!=2||returnType!="整数型")
            return conflict("现有回调须返回整数型并接收 WinId、msg 两个参数，请核对签名");
        std::istringstream declarations(routine);std::string declaration;size_t index=0;
        for(const auto& type:std::vector<std::string>{"整数型","文本型"}) {
            bool valid=false;
            while(std::getline(declarations,declaration)) {
                const auto compact=Compact(declaration);
                if(!compact.starts_with(".参数"))continue;
                const auto first=compact.find(','),second=first==compact.npos?compact.npos:compact.find(',',first+1);
                valid=first!=compact.npos&&compact.substr(first+1,second==compact.npos?second:second-first-1)==type;
                if(second!=compact.npos) {
                    const auto third=compact.find(',',second+1);
                    const auto flags=compact.substr(second+1,third==compact.npos?third:third-second-1);
                    valid=valid&&flags.find("传址")==flags.npos&&flags.find("参考")==flags.npos&&flags.find("数组")==flags.npos;
                }
                break;
            }
            if(!valid)return conflict("回调第 "+std::to_string(++index)+" 个参数类型不匹配，未写入代码");
            ++index;
        }
    }
    if(registrations>1)return conflict("同一个列表事件重复绑定，未自动修改");
    out.eventStatementPresent=registrations==1;
    out.eventComplete=callbackCount==1&&out.eventStatementPresent;
    out.startupComplete=out.eventComplete;
    out.status=out.eventComplete?"complete":"missing";
    out.message=out.eventComplete?"已使用易语言绑定对象，事件回调完整":"需补齐对象事件或回调子程序";
    return out;
}
}
