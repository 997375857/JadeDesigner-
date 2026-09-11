import { parse } from 'parse5';
const forbidden = new Set(['script','style','template','textarea','noscript','svg','math','iframe','xmp']);
export function locateText(source, descriptor) {
  const root = parse(source, { sourceCodeLocationInfo: true });
  let node = root;
  for (const step of descriptor.path) {
    if (forbidden.has(step.tag)) throw new Error('这个区域不支持文字编辑');
    const children = (node.childNodes || []).filter(n => n.tagName === step.tag);
    node = children[step.index];
    if (!node) throw new Error('动态生成或结构已改变，无法对应 HTML');
  }
  if (descriptor.id && node.attrs?.find(a => a.name === 'id')?.value !== descriptor.id)
    throw new Error('源文件控件与页面不一致');
  const texts = (node.childNodes || []).filter(n => n.nodeName === '#text' && n.value.trim());
  if (texts.length !== 1 || texts[0].value !== descriptor.text)
    throw new Error('动态文案或多个文字节点：请在 HTML / JS 中修改');
  const location = texts[0].sourceCodeLocation;
  if (!location) throw new Error('没有可靠的源文件位置');
  return { start: location.startOffset, end: location.endOffset,
    old: source.slice(location.startOffset, location.endOffset) };
}
export function describeText(element) {
  if (!(element instanceof Element) || element.closest('script,style,template,textarea,svg,math,iframe,[contenteditable]'))
    throw new Error('这个区域不支持文字编辑');
  const texts = [...element.childNodes].filter(n => n.nodeType === 3 && n.nodeValue.trim());
  if (texts.length !== 1) throw new Error('请选择一段静态文字，不支持输入值或混合文字');
  const path = [];
  for (let n = element; n; n = n.parentElement) {
    const siblings = [...(n.parentNode?.children || [])].filter(s => s.localName === n.localName);
    path.unshift({ tag: n.localName, index: siblings.indexOf(n) });
  }
  return { path, id: element.id, text: texts[0].nodeValue, node: texts[0] };
}
