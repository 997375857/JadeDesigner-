#pragma once
#include "DesignerInspection.h"
#include <functional>

namespace ProjectHealth {
struct Check { std::string state,title,detail; };
using Reader=std::function<int(const std::string&,std::string&,std::string&)>;
inline bool DirectCall(const std::string& source,const std::string& name) {
    std::istringstream input(source); std::string line;
    while(std::getline(input,line)) {
        std::string compact;
        for(char c:line) if(c!=' '&&c!='\t'&&c!='\r') compact+=c;
        if(compact==name+"()" || compact==name+"（）") return true;
    }
    return false;
}
inline std::vector<Check> Inspect(const Reader& assembly,const Reader& routine) {
    std::vector<Check> out;
    std::string common,subscriptions,error;
    const int c=assembly("Jade_公共基础",common,error);
    out.push_back({c<0?"unknown":c==0?"warning":"ok","公共程序集",c<0?"内存读取未完成："+error:c==0?"未使用生成模板；若有自定义初始化，可保留原方案":"已读取，未修改源码"});
    if(c==1) {
        const auto rs=DesignerInspection::Routines(common);
        const auto n=DesignerInspection::Count(rs,"Jade_公共_启动");
        out.push_back({n==1?"ok":"error","公共启动子程序",n==1?"名称唯一":"缺失或重复，未自动修复"});
        bool current=false;
        for(const auto& r:rs) if(r.name=="Jade_公共_生成配置") {
            std::istringstream lines(r.source); std::string line;
            while(std::getline(lines,line)) {
                const auto start=line.find_first_not_of(" \t");
                if(start!=line.npos && line.compare(start,std::string("返回").size(),"返回")==0 && line.find("v3;t=")!=line.npos) current=true;
            }
        }
        out.push_back({current?"ok":"warning","模板版本标记",current?"发现 v3 标记；不代表正文未被修改":"旧模板或无标记，只提示，不覆盖"});
        for(const auto& name:{"_启动子程序","_启动窗口_创建完毕"}) {
            std::string text,why; const int nread=routine(name,text,why);
            const bool direct=nread==1 && DirectCall(text,"Jade_公共_启动");
            out.push_back({"unknown",std::string("启动接入 · ")+name,nread<0?"无法读取："+why:direct?"发现直接调用；入口是否实际执行、调用条件及重复初始化仍需运行核验":"未发现直接调用；可能经其他子程序调用，不据此判定缺失"});
        }
    }
    const int s=assembly("Jade_通讯_订阅集",subscriptions,error);
    if(s!=1) out.push_back({s<0?"unknown":"warning","控件订阅",s<0?"内存读取未完成："+error:"订阅程序集尚未创建"});
    else {
        const auto rs=DesignerInspection::Routines(subscriptions);
        const int fixed=DesignerInspection::Count(rs,"Jade_通讯_订阅");
        out.push_back({fixed==1?"ok":"error","固定订阅入口",fixed==1?"名称唯一":"缺失或重复"});
        std::map<std::string,int> names,channels;
        int inspected=0;
        for(const auto& r:rs) {
            if(++names[r.name]>1) out.push_back({"error","重复回调",r.name});
            if(r.name!="Jade_通讯_订阅") continue;
            std::istringstream lines(r.source); std::string line;
            while(std::getline(lines,line)) {
                std::string channel,handler;
                if(!DesignerInspection::Subscription(line,channel,handler)) continue;
                if(inspected++>=300){out.push_back({"unknown","检查上限","本轮只检查前 300 条订阅"});return out;}
                if(++channels[channel]>1) out.push_back({"error","重复频道",channel});
                std::string callback,why;
                const int found=routine(handler,callback,why);
                out.push_back({found<0?"unknown":found==0?"error":"ok","订阅 · "+channel,found<0?"回调不唯一或无法读取："+handler:found==0?"回调不存在："+handler:"回调存在："+handler+"（业务未运行核验）"});
            }
        }
    }
    return out;
}
}
