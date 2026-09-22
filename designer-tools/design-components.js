// Presets reuse existing Jade control types; they are not new module APIs.
export const PRESETS={
  'icon-button':{label:'图标按钮',kind:'button',width:40,height:40,text:'',style:{icon:'plus',align:'center'}},
  'outline-button':{label:'描边按钮',kind:'button',style:{variant:'outline',align:'center'}},
  'tonal-button':{label:'柔色按钮',kind:'button',style:{variant:'tonal',align:'center'}},
  'text-button':{label:'文字按钮',kind:'button',style:{variant:'text',align:'center'}},
  'floating-button':{label:'悬浮按钮',kind:'button',width:56,height:56,text:'',style:{icon:'plus',radius:28,align:'center'}},
  textarea:{label:'多行编辑框',kind:'edit',width:260,height:120,placeholder:'请输入内容'},
  password:{label:'密码编辑框',kind:'edit',placeholder:'请输入密码'},
  search:{label:'搜索框',kind:'edit',width:240,placeholder:'搜索',style:{icon:'search'}},
  switch:{label:'开关',kind:'checkbox',width:140},
  heading:{label:'标题',kind:'label',width:240,height:48,style:{fontSize:24}},
  badge:{label:'标签徽标',kind:'label',width:100,height:32,style:{radius:16,align:'center'}},
  card:{label:'内容卡片',kind:'container',width:280,height:180},
  toolbar:{label:'工具栏',kind:'container',width:480,height:56},
  sidebar:{label:'侧边导航',kind:'container',width:200,height:320},
  'navigation-rail':{label:'窄侧导航',kind:'container',width:96,height:400,style:{radius:0}},
  dialog:{label:'对话框',kind:'container',width:360,height:220},
  divider:{label:'分隔线',kind:'container',width:280,height:24,style:{radius:0}},
  rectangle:{label:'矩形',kind:'container',width:180,height:120,style:{radius:0}},
  ellipse:{label:'椭圆',kind:'container',width:120,height:120,style:{radius:100}}
};
export const COMPONENT_GROUPS=[
  ['按钮',['button','icon-button','outline-button','tonal-button','text-button','floating-button']],
  ['操作与输入',['edit','textarea','password','search','select','checkbox','radio','switch','number','slider']],
  ['数据与导航',['super-list','tree','tabs','sidebar','navigation-rail']],
  ['内容与反馈',['label','heading','badge','progress']],
  ['布局与形状',['container','card','toolbar','dialog','divider','rectangle','ellipse']]
];
export const NAV_ICONS={house:'首页',search:'搜索',heart:'收藏',settings:'设置',user:'用户',bell:'通知',folder:'文件',chart:'统计'};
const DEFAULT_NAVIGATION={menu:true,selectedId:'item-1',items:[
  {id:'item-1',text:'首页',icon:'house'},
  {id:'item-2',text:'搜索',icon:'search'},
  {id:'item-3',text:'收藏',icon:'heart'},
  {id:'item-4',text:'设置',icon:'settings'}
]};
export const isNavigation=n=>['sidebar','navigation-rail'].includes(n.preset);
export const navigationOf=n=>n.navigation||JSON.parse(JSON.stringify(DEFAULT_NAVIGATION));
export const RESIZE_DIRECTIONS={nw:'左上',n:'上边',ne:'右上',e:'右边',se:'右下',s:'下边',sw:'左下',w:'左边'};
export function resizeGeometry(node,direction,dx,dy,bounds){
  let left=node.x,top=node.y,right=left+node.width,bottom=top+node.height;
  const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
  if(direction.includes('w'))left=clamp(left+dx,0,right-24);
  if(direction.includes('e'))right=clamp(right+dx,left+24,bounds.width);
  if(direction.includes('n'))top=clamp(top+dy,0,bottom-24);
  if(direction.includes('s'))bottom=clamp(bottom+dy,top+24,bounds.height);
  return {x:left,y:top,width:right-left,height:bottom-top};
}
