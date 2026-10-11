// Document mutation and traversal, independent of visual layout.
document.hasFocus = () => true;
Object.defineProperties(document, {
  hidden: {get: () => false, configurable:true},
  visibilityState: {get: () => 'visible', configurable:true}
});
document.createComment = text => __wrap(__webcliCreateComment(String(text ?? '')));
document.importNode = function(node, deep = false) {
  if (!(node instanceof DomNode)) throw new TypeError('importNode requires a Node');
  if (node.nodeType === 9 || node instanceof globalThis.ShadowRoot)
    throw new DOMException('This node cannot be imported', 'NotSupportedError');
  const clone = __wrap(__webcliClone(node.__id, Boolean(deep), this.__id));
  if (typeof __connectComponents === 'function') __connectComponents(clone);
  return clone;
};
document.write = function(...parts) {
  const html = parts.map(String).join('');
  if (!html) return;
  if (document.readyState !== 'loading' || !document.currentScript)
    throw new Error('document.write requires an executing parser script');
  __webcliDocumentWrite(html);
};
document.writeln = (...parts) => document.write(...parts, '\n');
globalThis.NodeFilter = Object.freeze({FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3,
  SHOW_ALL: 0xffffffff, SHOW_ELEMENT: 1, SHOW_TEXT: 4, SHOW_COMMENT: 128, SHOW_DOCUMENT: 256,
  SHOW_DOCUMENT_FRAGMENT: 1024});
document.createTreeWalker = function(root, whatToShow = NodeFilter.SHOW_ALL, filter = null) {
  if (!(root instanceof DomNode)) throw new TypeError('TreeWalker requires a DOM root');
  const verdict = node => !(whatToShow & (1 << (node.nodeType - 1))) ? NodeFilter.FILTER_SKIP :
    filter ? (typeof filter === 'function' ? filter(node) : filter.acceptNode(node)) : NodeFilter.FILTER_ACCEPT;
  return {root, whatToShow, filter, currentNode: root,
    nextNode() {
      let node = this.currentNode;
      let descend = true;
      while (node) {
        if (descend && node.firstChild) node = node.firstChild;
        else {
          while (node && node !== root && !node.nextSibling) node = node.parentNode;
          if (!node || node === root) return null;
          node = node.nextSibling;
        }
        const result = verdict(node);
        if (result === NodeFilter.FILTER_ACCEPT) { this.currentNode = node; return node; }
        descend = result !== NodeFilter.FILTER_REJECT;
      }
      return null;
    },
    parentNode() {
      for (let node = this.currentNode.parentNode; node; node = node.parentNode) {
        if (verdict(node) === NodeFilter.FILTER_ACCEPT) {this.currentNode = node; return node;}
        if (node === root) break;
      }
      return null;
    }
  };
};

Object.defineProperty(document, 'cookie', {get: () => __webcliCookie(location.href),
  set: value => __webcliCookie(location.href, String(value))});

// Image resources fetch without rendering or exposing cross-origin response data.
function __loadImage(node) {
  if (node.ownerDocument !== document) return;
  const generation = node._imageGeneration = (node._imageGeneration || 0) + 1;
  node.complete = false;
  const finish = type => {
    if (node._imageGeneration !== generation) return;
    node.complete = true;
    try { node.dispatchEvent(new Event(type)); }
    catch (error) { __webcliReportError(error?.stack || String(error)); }
  };
  if (!node.getAttribute('src')) {node.complete = true; return;}
  try {
    __webcliLoadImage(node.src, node.__id).then(result => finish(result.loaded ? 'load' : 'error'),
      () => finish('error'));
  } catch (_) {queueMicrotask(() => finish('error'));}
}
globalThis.Image = function(width, height) {
  const image = document.createElement('img'); image.complete = true;
  if (width !== undefined) image.setAttribute('width', width);
  if (height !== undefined) image.setAttribute('height', height);
  return image;
};

// Dialogs expose state and events to agents without a visual top layer.
class HTMLDialogElement extends Element {
  constructor() {return document.createElement('dialog');}
  static [Symbol.hasInstance](node) {return node instanceof DomNode && node.tagName === 'DIALOG';}
}
for (const [name, descriptor] of Object.entries({
  open: {get() {return this.hasAttribute('open');}, set(value) {this.toggleAttribute('open', !!value);}},
  returnValue: {get() {return this._dialogReturnValue || '';}, set(value) {this._dialogReturnValue = String(value);}},
  closedBy: {get() {const value = this.getAttribute('closedby'); return ['any','closerequest','none'].includes(value) ? value : this._dialogModal ? 'closerequest' : 'none';},
    set(value) {this.setAttribute('closedby', value);}},
  show: {value: function() {if (this.tagName !== 'DIALOG') throw new TypeError('dialog required'); if (this.open && this._dialogModal) throw new Error('dialog is already modal'); this.open = true;}},
  showModal: {value: function() {if (this.tagName !== 'DIALOG' || !this.isConnected) throw new Error('connected dialog required'); if (this.open && !this._dialogModal) throw new Error('dialog is already nonmodal'); this._dialogModal = true; this.open = true;}},
  close: {value: function(value) {if (this.tagName !== 'DIALOG') throw new TypeError('dialog required'); if (!this.open) return; if (value !== undefined) this.returnValue = value; this.open = false; this._dialogModal = false; queueMicrotask(() => this.dispatchEvent(new Event('close')));}},
  requestClose: {value: function(value) {if (this.open && this.dispatchEvent(new Event('cancel', {cancelable:true}))) this.close(value);}}
})) {
  Object.defineProperty(HTMLDialogElement.prototype, name, {...descriptor, configurable:true});
}
globalThis.HTMLDialogElement = HTMLDialogElement;
for (const node of __cachedNodes()) if (node.tagName === 'DIALOG') Object.setPrototypeOf(node, HTMLDialogElement.prototype);
