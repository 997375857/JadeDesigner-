// Public JadeView control capabilities used by the designer UI.
// Internal lifecycle/cache helpers are intentionally excluded.
const superListEvents = [
  ['被单击', 'click'], ['被双击', 'dblclick'], ['表项被选中', 'select'],
  ['选择框被点击', 'checkbox-change'], ['单选框被点击', 'radio-change'],
  ['右键单击', 'contextmenu'], ['鼠标进入表项', 'mouseenter'],
  ['鼠标离开表项', 'mouseleave'], ['滚动到底部', 'scroll-bottom']
].map(([label, code]) => ({label, code}));

export const JADE_CONTROL_METADATA = {
  'super-list': {
    label: '超级列表框', events: superListEvents,
    methods: [
      ['插入表项','插入行并返回索引'],['删除表项','删除指定行'],['清空','清空全部行'],['刷新','刷新网页显示'],
      ['查找表项','按标题查找行'],['取表项数量','读取行数'],['取选中项','读取当前选中行'],['置选中项','设置当前选中行'],
      ['取标题','读取单元格文本'],['置标题','设置单元格文本'],['取图片索引','读取图片索引'],['置图片','设置图片索引'],
      ['取状态图片','读取状态图片索引'],['置状态图片','设置状态图片索引'],['取缩进数目','读取缩进数'],['置缩进数目','设置缩进数'],
      ['取表项数值','读取行关联数值'],['置表项数值','设置行关联数值'],['取列标题','读取列标题'],['置列标题','设置列标题'],
      ['取列数量','读取列数'],['取列宽','读取列宽'],['置列宽','设置列宽'],['置选择框','设置单元格选择框'],
      ['取选择框','读取选择框状态'],['置选择框状态','设置选择框状态'],['全选选择框','批量设置选择框状态'],
      ['置单选框','设置单元格单选框'],['取单选框','读取单选框状态'],['取单选框索引','读取选中单选框索引'],
      ['置单选框状态','设置单选框状态'],['选择框置样式','设置选择框样式'],['选择框宽高','设置选择框默认尺寸'],
      ['禁止重画','暂停批量刷新'],['允许重画','恢复批量刷新'],['绑定事件','绑定网页事件回调']
    ]
  },
  button: {label:'按钮', events:[{label:'被单击',code:'click'}]},
  // Independent checkbox/radio change callbacks remain intentionally hidden;
  // row-level checkbox/radio events belong to the super-list contract.
  checkbox: {label:'复选框', events:[]},
  radio: {label:'单选框', events:[]},
  select: {label:'下拉框', events:[{label:'选择项被改变',code:'change'}]}
};

export function controlKind(element) {
  if (!element) return '';
  const declared=element.dataset?.jadeControl||'';
  if (declared==='list'||declared==='super-list') return 'super-list';
  if (declared) return declared;
  if (element.matches?.('button,[role="button"],input[type="button"],input[type="submit"]')) return 'button';
  if (element.matches?.('input[type="checkbox"]')) return 'checkbox';
  if (element.matches?.('input[type="radio"]')) return 'radio';
  if (element.matches?.('select')) return 'select';
  return '';
}

export function metadataFor(element) { return JADE_CONTROL_METADATA[controlKind(element)]||null; }

export const stableControlId=element=>element?.dataset?.jadeId||element?.id||'';
export function controlTarget(source) {
  if (!(source instanceof Element)) source=source?.parentElement;
  // Row/cell IDs belong to the list, not to separately bindable controls.
  const list=source?.closest('[data-jade-control="list"],[data-jade-control="super-list"]');
  if (list) return list;
  return source?.closest('[data-jade-control],[data-jade-id],button,[role="button"],input,select,textarea')||null;
}
export function uniqueControlId(element) {
  const id=stableControlId(element);
  return !!id && [...document.querySelectorAll('[data-jade-id],[id]')]
    .filter(candidate=>candidate.dataset.jadeId===id||candidate.id===id).length===1;
}
