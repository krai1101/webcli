const __attributeState = new WeakMap();
const __attributeViews = new WeakMap();
function __attrState(attr) {
  const state = __attributeState.get(attr);
  if (!state) throw new TypeError('Illegal Attr invocation');
  return state;
}
globalThis.Attr = class Attr extends DomNode {
  constructor() { throw new TypeError('Illegal constructor'); }
  get name() { return __attrState(this).name; }
  get nodeName() { return this.name; }
  get nodeType() { return 2; }
  get namespaceURI() { return __attrState(this).namespaceURI; }
  get localName() { return __attrState(this).localName; }
  get prefix() {
    const state = __attrState(this);
    return state.namespaceURI && state.name.includes(':') ? state.name.split(':')[0] : null;
  }
  get ownerElement() { return __attrState(this).owner; }
  get ownerDocument() { return document; }
  get parentNode() { return null; }
  get parentElement() { return null; }
  get previousSibling() { return null; }
  get nextSibling() { return null; }
  get firstChild() { return null; }
  get lastChild() { return null; }
  get childNodes() { return []; }
  get isConnected() { return false; }
  get specified() { __attrState(this); return true; }
  get value() {
    const state = __attrState(this);
    return state.owner ? state.owner.getAttribute(state.name) ?? state.value : state.value;
  }
  set value(value) {
    const state = __attrState(this);
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol to a string');
    state.value = String(value);
    if (state.owner) state.owner.setAttribute(state.name, state.value);
  }
  get nodeValue() { return this.value; }
  set nodeValue(value) { this.value = value ?? ''; }
  get textContent() { return this.value; }
  set textContent(value) { this.value = value ?? ''; }
};
function __attributeView(owner, entry) {
  let views = __attributeViews.get(owner);
  if (!views) __attributeViews.set(owner, views = new Map());
  let attr = views.get(entry.name);
  if (!attr) {
    attr = Object.create(Attr.prototype);
    __attributeState.set(attr, { ...entry, owner });
    views.set(entry.name, attr);
  }
  return attr;
}
function __detachAttribute(owner, name, value) {
  const views = __attributeViews.get(owner);
  const attr = views && views.get(name);
  if (!attr) return;
  const state = __attributeState.get(attr);
  state.value = value;
  state.owner = null;
  views.delete(name);
}
