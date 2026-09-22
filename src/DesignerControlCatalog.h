#pragma once
#include <string_view>
// Generated from jade-control-metadata.js by build.mjs.
namespace DesignerControlCatalog {
struct Event {std::string_view type,label,code;};
inline constexpr Event Events[]={{"super-list","被单击","click"},
{"super-list","被双击","dblclick"},
{"super-list","表项被选中","select"},
{"super-list","选择框被点击","checkbox-change"},
{"super-list","单选框被点击","radio-change"},
{"super-list","右键单击","contextmenu"},
{"super-list","鼠标进入表项","mouseenter"},
{"super-list","鼠标离开表项","mouseleave"},
{"super-list","滚动到底部","scroll-bottom"},
{"button","被单击","click"},
{"select","选择项被改变","change"}};
struct Methods {std::string_view type,names;};
inline constexpr Methods PublicMethods[]={{"super-list","插入表项、删除表项、清空、刷新、查找表项、取表项数量、取选中项、置选中项、取标题、置标题、取图片索引、置图片、取状态图片、置状态图片、取缩进数目、置缩进数目、取表项数值、置表项数值、取列标题、置列标题、取列数量、取列宽、置列宽、置选择框、取选择框、置选择框状态、全选选择框、置单选框、取单选框、取单选框索引、置单选框状态、选择框置样式、选择框宽高、禁止重画、允许重画、绑定事件"},
{"button",""},
{"checkbox",""},
{"radio",""},
{"select",""}};
inline const Event* Find(std::string_view type,std::string_view code) {
  if(type=="list")type="super-list";
  for(const auto& event:Events)if(event.type==type&&event.code==code)return &event;
  return nullptr;
}
}
