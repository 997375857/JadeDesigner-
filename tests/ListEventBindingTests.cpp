#include "../src/ListEventBinding.h"
#include "../src/DesignerControlCatalog.h"
#include <cstdio>
#include <cstdlib>

namespace {
int checks=0;
void Check(bool value,const char* name) {
    if(!value){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}
    ++checks;std::printf("PASS %s\n",name);
}
struct Project {
    std::map<std::string,std::string> assemblies;
    bool unavailable=false;
    ListEventBinding::Reader Assembly() {
        return [this](const auto& name,auto& source,auto& error){
            if(unavailable){error="busy";return -1;}
            const auto found=assemblies.find(name);
            if(found==assemblies.end())return 0;
            source=found->second;return 1;
        };
    }
    ListEventBinding::Reader Routine() {
        return [this](const auto& name,auto& source,auto& error){
            if(unavailable){error="busy";return -1;}
            int count=0;
            for(const auto& [assembly,text]:assemblies) {
                (void)assembly;
                for(const auto& routine:DesignerInspection::Routines(text))
                    if(routine.name==name){++count;source=routine.source;}
            }
            return count>1?-1:count;
        };
    }
    ListEventBinding::Inspection Inspect(const ListEventBinding::Plan& p) {
        return ListEventBinding::Inspect(p,Assembly(),Routine());
    }
    void Complete(const ListEventBinding::Plan& p) {
        auto source=p.source;
        assemblies[p.assembly]=source;
        auto& subscription=assemblies[ListEventBinding::SubscriptionAssembly];
        subscription=".版本 2\n.程序集 Jade_通讯_订阅集\n.子程序 Jade_通讯_订阅\n"+p.startup+"\n"+
            (p.callbackAssembly == ListEventBinding::SubscriptionAssembly ? p.callback : "");
        if (p.callbackAssembly != ListEventBinding::SubscriptionAssembly)
            assemblies[p.callbackAssembly] += p.callback;
    }
};
}
int main() {
    using namespace ListEventBinding;
    const auto p=Make("主播列表","click","被单击","主播列表_被单击","");
    Check(SafeId(p.id)&&!SafeId("")&&!SafeId("\"\n代码")&&!SafeId("“文本”"),"validate source literals");
    Check(Token("a-b")!=Token("a_b")&&Token("A")!=Token("a"),"stable names distinguish sanitized and case collisions");
    Check(p.source.find(".程序集变量 "+p.object+", JadeView超级列表框对象")!=std::string::npos,"object lifetime belongs to assembly");
    Check(p.source.find(".DLL命令")==std::string::npos&&p.source.find(".类")==std::string::npos,"no new DLL declarations or factory classes");
    Check(p.assignment.find("Jade超级列表框绑定 (\"主播列表\")")!=std::string::npos,"binder uses actual control id and optional main window");
    const auto userBindingSource=R"E(.版本 2
.程序集 UI测试
.程序集变量 全局_主播列表, JadeView超级列表框对象

.子程序 UI初始化
全局_主播列表 ＝ Jade超级列表框绑定 ("主播列表", 全局_内部_主窗口ID, )
)E";
    const auto located=FindExistingBinding(userBindingSource,"主播列表");
    Check(located.found&&!located.ambiguous&&located.object=="全局_主播列表"&&located.routine=="UI初始化",
        "existing binding resolves the user's object and routine");
    Check(HandlerName(located.object,"被单击")=="全局_主播列表被单击"&&
        HandlerName(located.object,"被双击")=="全局_主播列表被双击",
        "list callbacks follow the bound object name");
    Check(CallbackSource(HandlerName(located.object,"被双击")).find(
        ".子程序 全局_主播列表被双击, 整数型")!=std::string::npos,
        "callback source follows the resolved object callback name");
    const std::string actualBindings=R"E(.版本 2
.程序集 Jade_通讯_订阅集
.子程序 Jade_通讯_订阅
全局_主播列表 ＝ Jade超级列表框绑定 ("streamer-list", 全局_内部_主窗口ID, )
全局_弹幕列表 ＝ Jade超级列表框绑定 ("danmaku-list", 全局_内部_主窗口ID, )
)E";
    const auto streamer=FindExistingBinding(actualBindings,"streamer-list");
    const auto danmaku=FindExistingBinding(actualBindings,"danmaku-list");
    Check(streamer.found&&streamer.object=="全局_主播列表"&&
        danmaku.found&&!danmaku.ambiguous&&danmaku.object=="全局_弹幕列表"&&
        danmaku.routine=="Jade_通讯_订阅",
        "both real list bindings survive Chinese UTF-8 identifier bytes");
    for(const auto& object:std::vector<std::string>{"全局_弹幕列表","全局_测试列表","全局_全部列表"}) {
        const auto match=FindExistingBinding(".子程序 初始化\n"+object+
            " ＝ Jade超级列表框绑定 (\"id\")\n","id");
        Check(match.found&&match.object==object,"Chinese identifiers are not byte-matched against fullwidth punctuation");
    }
    Check(!FindExistingBinding(".子程序 初始化\n坏，变量 ＝ Jade超级列表框绑定 (\"id\")\n","id").found,
        "fullwidth comma still cannot occur in a binding object");
    bool nativeNewlines=true;
    for(size_t i=0;i<p.callback.size();++i)
        if(p.callback[i]=='\n'&&(i==0||p.callback[i-1]!='\r'))nativeNewlines=false;
    Check(nativeNewlines&&p.callback.find("\r\n")!=std::string::npos,
        "list callback uses CRLF like the working native subscription template");
    const auto duplicateBinding=std::string(userBindingSource)+R"(
.程序集变量 全局_主播列表2, JadeView超级列表框对象
.子程序 另一个初始化
全局_主播列表2 ＝ Jade超级列表框绑定 ("主播列表")
)";
    Check(FindExistingBinding(duplicateBinding,"主播列表").ambiguous,
        "duplicate control bindings are rejected");
    auto preferred=Make("主播列表","click","被单击","主播列表_被单击","");
    preferred.existingBinding=true;preferred.assembly="UI测试";preferred.object=located.object;
    preferred.initialize=located.routine;preferred.registration=preferred.object+".绑定事件 (\"被单击\", &主播列表_被单击)";
    preferred.source=userBindingSource;preferred.initializer.clear();
    Project preferredProject;preferredProject.assemblies[preferred.assembly]=std::string(userBindingSource)+"\n"+preferred.registration+"\n";
    preferredProject.assemblies[SubscriptionAssembly]=std::string(".版本 2\n.程序集 ")+SubscriptionAssembly+"\n";
    Check(InspectExisting(preferred,preferredProject.Assembly(),preferredProject.Routine()).status=="missing",
        "existing object binding is missing only its callback initially");
    Project project;
    Check(project.Inspect(p).status=="missing","empty project can be completed");
    project.assemblies[p.assembly]=p.source;
    Check(project.Inspect(p).status=="missing","partial assembly is retryable");
    project.Complete(p);
    Check(project.Inspect(p).status=="complete","object callback registration and startup form complete chain");
    auto alias=project;
    auto& aliasSource=alias.assemblies[p.assembly];
    const auto labelOffset=aliasSource.find(".绑定事件 (\"被单击\"");
    aliasSource.replace(labelOffset,std::string(".绑定事件 (\"被单击\"").size(),".绑定事件 (\"单击\"");
    Check(alias.Inspect(p).status=="complete","module event aliases do not generate duplicate subscriptions");
    const auto startupAt=alias.assemblies[SubscriptionAssembly].find(p.startup);
    alias.assemblies[SubscriptionAssembly].erase(startupAt,p.startup.size()+1);
    const auto interrupted=alias.Inspect(p);
    Check(interrupted.eventComplete&&!interrupted.startupComplete&&interrupted.status=="missing",
        "interrupted generation repairs startup only and preserves alias registration");
    Check(LineCount("ABC ()\n","abc ()")==1&&LineCount("返回 (\"ABC\")\n","返回 (\"abc\")")==0,
        "identifier case is ignored but literal control IDs remain case sensitive");
    const auto saved=project.assemblies;
    Check(project.Inspect(p).status=="complete"&&project.assemblies==saved,"repeated inspection preserves code byte for byte");
    auto custom=project;custom.assemblies[p.callbackAssembly]+="\n' business code is preserved\n";
    Check(custom.Inspect(p).status=="complete","business callback body is not regenerated");
    const auto second=Make(p.id,"dblclick","被双击","主播列表_被双击","");
    Check(second.assembly==p.assembly&&second.object==p.object,"events on one list share a persistent object");
    Check(project.Inspect(second).status=="missing","new event remains missing without resetting list");
    const auto other=Make("弹幕列表","click","被单击","弹幕列表_被单击","");
    Check(other.assembly!=p.assembly&&project.Inspect(other).status=="missing","multiple lists stay separate");
    auto duplicate=project;
    const auto duplicateAt=duplicate.assemblies[SubscriptionAssembly].find(p.startup);
    duplicate.assemblies[SubscriptionAssembly].insert(duplicateAt+p.startup.size(),"\n"+p.startup);
    Check(duplicate.Inspect(p).status=="conflict","duplicate startup is rejected");
    auto wrong=project;
    const auto wrongRegistration=p.object+".绑定事件 (\"被单击\", &其他回调)";
    wrong.assemblies[p.assembly].replace(wrong.assemblies[p.assembly].find(p.registration),p.registration.size(),wrongRegistration);
    Check(wrong.Inspect(p).status=="conflict","another callback for same event is rejected");
    auto legacy=project;
    const auto legacyAt=legacy.assemblies[SubscriptionAssembly].find(p.startup);
    legacy.assemblies[SubscriptionAssembly].insert(legacyAt+p.startup.size(),
        "\nJadeView.通讯.订阅 (\"jade:list:event:主播列表:click\", &主播列表_被单击)");
    Check(legacy.Inspect(p).status=="conflict","legacy raw subscription requires explicit merge");
    auto collision=project;collision.assemblies[p.assembly]=".程序集 "+p.assembly+"\n.子程序 用户代码\n";
    Check(collision.Inspect(p).status=="conflict","user assembly with generated name is not overwritten");
    Project foreign;foreign.assemblies["other"]=".程序集 other\n"+p.callback;
    Check(foreign.Inspect(p).status=="conflict","global callback collision blocks before any write");
    auto missingReturn=project;
    auto& code=missingReturn.assemblies[p.callbackAssembly];
    const auto pos=code.find(".子程序 "+p.handler+", 整数型");
    code.erase(pos+std::string(".子程序 "+p.handler).size(),std::string(", 整数型").size());
    Check(missingReturn.Inspect(p).status=="conflict","callback missing return type is rejected");
    auto wrongType=project;auto& typed=wrongType.assemblies[p.callbackAssembly];
    typed.replace(typed.find(".参数 msg, 文本型"),std::string(".参数 msg, 文本型").size(),".参数 msg, 整数型");
    Check(wrongType.Inspect(p).status=="conflict","callback payload must be text not integer");
    auto earlyStartup=project;
    const auto earlyAt=earlyStartup.assemblies[SubscriptionAssembly].find(p.startup);
    earlyStartup.assemblies[SubscriptionAssembly].insert(earlyAt+p.startup.size(),"\n返回 ()");
    Check(earlyStartup.Inspect(p).status=="conflict","startup with return is not silently appended to");
    auto early=project;auto& earlySource=early.assemblies[p.assembly];
    earlySource.replace(earlySource.find(p.registration),p.registration.size(),"返回 ()\n"+p.registration);
    Check(early.Inspect(p).status=="conflict","early return blocks unreachable appended binding");
    auto unavailable=project;unavailable.unavailable=true;
    Check(unavailable.Inspect(p).status=="unknown","unavailable memory never treated as absent");
    Check(LineCount("  对象．绑定事件 （“被单击”， &回调） '保留\n","对象.绑定事件 (\"被单击\", &回调)")==1,"fullwidth IDE serialization is normalized");
    const auto external=Make("外部列表","click","被单击","外部回调","用户事件集");
    Project separated;separated.Complete(external);
    Check(separated.Inspect(external).status=="complete","explicit callback assembly is preserved");
    int listEvents=0;for(const auto& entry:DesignerControlCatalog::Events)if(entry.type=="super-list")++listEvents;
    Check(listEvents==9&&DesignerControlCatalog::Find("list","dblclick"),"all nine module list events are shared with native UI");
    Check(!DesignerControlCatalog::Find("button","dblclick")&&!DesignerControlCatalog::Find("checkbox","change"),"unsupported independent events are hidden");
    std::printf("%d list event checks passed\n",checks);
}
