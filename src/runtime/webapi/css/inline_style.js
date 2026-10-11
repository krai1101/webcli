function __cssPropertyName(name) {
  return name === 'cssFloat' ? 'float' : String(name).replace(/[A-Z]/g, c => '-' + c.toLowerCase());
}
const __styleNodes = new WeakMap();
function __styleNode(style) {
  const node = __styleNodes.get(style);
  if (!node) throw new TypeError('Illegal CSSStyleDeclaration invocation');
  return node;
}
function __styleDeclarations(style) {
    const node = __styleNode(style);
    const declarations = new Map();
    for (const declaration of (node.getAttribute('style') || '').split(';')) {
      const colon = declaration.indexOf(':');
      if (colon >= 0) declarations.set(declaration.slice(0, colon).trim(), declaration.slice(
        colon + 1).trim());
    }
    return declarations;
}
globalThis.CSSStyleDeclaration = class CSSStyleDeclaration {
    constructor() { throw new TypeError('Illegal constructor'); }
    get cssText() {
      return __styleNode(this).getAttribute('style') || '';
    }
    set cssText(value) {
      __styleNode(this).setAttribute('style', String(value));
    }
    get length() {
      return __styleDeclarations(this).size;
    }
    item(index) {
      return [...__styleDeclarations(this).keys()][index] || '';
    }
    getPropertyValue(name) {
      const value = (__styleDeclarations(this).get(String(name)) || '').replace(/\s*!important\s*$/i, '').trim();
      // CSSOM serializes a numeric opacity with its leading zero. Libraries use
      // this standard representation to detect modern CSS support.
      if (name === 'opacity' && /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$/.test(value))
        return String(Number(value));
      return value;
    }
    getPropertyPriority(name) {
      return /!important\s*$/i.test(__styleDeclarations(this).get(String(name)) || '') ? 'important' : '';
    }
    setProperty(name, value, priority = '') {
      const values = __styleDeclarations(this);
      if (value === '') values.delete(String(name));
      else values.set(String(name), String(value) + (priority === 'important' ? ' !important' :
        ''));
      __styleNode(this).setAttribute('style', [...values].map(([key, entry]) => key + ': ' + entry + ';').join(
        ' '));
    }
    removeProperty(name) {
      const old = this.getPropertyValue(name);
      this.setProperty(name, '');
      return old;
    }
};
Object.defineProperty(CSSStyleDeclaration.prototype, Symbol.toStringTag, {
  value: 'CSSStyleDeclaration', configurable: true
});
function __inlineStyle(node) {
  const style = Object.create(CSSStyleDeclaration.prototype);
  __styleNodes.set(style, node);
  const proxy = new Proxy(style, {
    get(target, key) {
      return key in target ? Reflect.get(target, key) : typeof key === 'string' ? target
        .getPropertyValue(__cssPropertyName(key)) : undefined;
    },
    set(target, key, value) {
      if (key in target) Reflect.set(target, key, value);
      else target.setProperty(__cssPropertyName(key), String(value));
      return true;
    }
  });
  __styleNodes.set(proxy, node);
  return proxy;
}
