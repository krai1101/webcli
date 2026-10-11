const __clickingNodes = new WeakSet();
const __nodeCache = new Map();
const __elementTypes = new Map();
const __parsedOwners = new Map(), __parsedOwner = Symbol('parsedDocumentOwner');
function* __cachedNodes() {
  for (const value of __nodeCache.values()) {
    const node = value instanceof WeakRef ? value.deref() : value;
    if (node) yield node;
  }
}
function __cacheNode(id, node, lease = null) {
  const previous = node[__parsedOwner];
  if (previous) {
    previous.nodes.delete(id);
    delete node[__parsedOwner];
  }
  lease ||= __parsedOwners.get(__webcliOwnerDocument(id))?.deref() || null;
  if (lease) {
    Object.defineProperty(node, __parsedOwner, {value: lease, configurable: true});
    lease.nodes.set(id, node);
    __nodeCache.set(id, new WeakRef(node));
  } else __nodeCache.set(id, node);
}

function __refreshNodeOwners() {
  // Native adoption rebinds IDs. Keep detached document leases in step with
  // those IDs before JavaScript can collect either arena.
  for (const node of __cachedNodes()) {
    if (node.nodeType === 9) continue;
    const owner = __webcliOwnerDocument(node.__id);
    const previous = node[__parsedOwner];
    if ((previous?.rootId ?? document.__id) !== owner) __cacheNode(node.__id, node);
  }
}

function __wrap(id) {
  if (id < 0) return null;
  let cached = __nodeCache.get(id);
  if (cached instanceof WeakRef) {
    cached = cached.deref();
    if (cached) return cached;
    __nodeCache.delete(id);
  } else if (cached) return cached;
  const ownerId = __webcliNodeType(id) === 9 ? id : __webcliOwnerDocument(id);
  const lease = __parsedOwners.get(ownerId)?.deref() || null;
  if (!__nodeCache.has(id)) {
    const node = new DomNode(id);
    const type = __webcliNodeType(id);
    const element = type === 1;
    if (type === 3 && globalThis.Text) Object.setPrototypeOf(node, Text.prototype);
    else if (type === 8 && globalThis.Comment) Object.setPrototypeOf(node, Comment.prototype);
    else if (type === 10 && globalThis.DocumentType)
      Object.setPrototypeOf(node, DocumentType.prototype);
    if (element && globalThis.SVGElement && node.namespaceURI === __svgNamespace) Object
      .setPrototypeOf(node, SVGElement.prototype);
    else if (globalThis.HTMLElement && globalThis.HTMLElement !== DomNode && element && node
      .namespaceURI === __htmlNamespace) Object.setPrototypeOf(node, (__elementTypes.get(node
      .tagName) || HTMLElement).prototype);
    else if (element && globalThis.Element && Element !== DomNode)
      Object.setPrototypeOf(node, Element.prototype);
    if (globalThis.HTMLDialogElement && node.tagName === 'DIALOG' && node.namespaceURI ===
      __htmlNamespace) Object.setPrototypeOf(node, HTMLDialogElement.prototype);
    __cacheNode(id, node, lease);
    return node;
  }
}
class DomNode extends EventTarget {
  constructor(id) {
    super();
    this.__id = id;
  }
  get nodeType() {
    return this === document ? 9 : __webcliNodeType(this.__id);
  }
  get nodeName() {
    if (this.nodeType === 10) return __webcliNodeName(this.__id);
    return this.nodeType === 8 ? '#comment' : this.nodeType === 3 ? '#text' : this.nodeType ===
      9 ? '#document' : this.nodeType === 11 ? '#document-fragment' : __webcliNodeName(this.__id)
      .toUpperCase();
  }
  get tagName() {
    return this.nodeType === 1 ? this.nodeName : undefined;
  }
  get ownerDocument() {
    return __wrap(__webcliOwnerDocument(this.__id));
  }
  get parentNode() {
    return this === document ? null : __wrap(__webcliParent(this.__id));
  }
  get parentElement() {
    const parent = this.parentNode;
    return parent && parent.nodeType === 1 ? parent : null;
  }
  get childNodes() {
    return __webcliChildren(this.__id).map(__wrap);
  }
  get children() {
    return this.childNodes.filter(n => n.nodeType === 1);
  }
  get firstChild() {
    return this.childNodes[0] || null;
  }
  get lastChild() {
    const children = this.childNodes;
    return children[children.length - 1] || null;
  }
  get nextSibling() {
    const parent = this.parentNode;
    if (!parent) return null;
    const siblings = parent.childNodes;
    return siblings[siblings.indexOf(this) + 1] || null;
  }
  get previousSibling() {
    const parent = this.parentNode;
    if (!parent) return null;
    const siblings = parent.childNodes;
    return siblings[siblings.indexOf(this) - 1] || null;
  }
  get firstElementChild() {
    return this.children[0] || null;
  }
  get lastElementChild() {
    const children = this.children;
    return children[children.length - 1] || null;
  }
  get childElementCount() {
    return this.children.length;
  }
  get nextElementSibling() {
    let node = this.nextSibling;
    while (node && node.nodeType !== 1) node = node.nextSibling;
    return node;
  }
  get previousElementSibling() {
    let node = this.previousSibling;
    while (node && node.nodeType !== 1) node = node.previousSibling;
    return node;
  }
  get isConnected() {
    for (let n = this; n; n = n.parentNode)
      if (n === document) return true;
    return false;
  }
  get textContent() {
    return __webcliTextGet(this.__id);
  }
  set textContent(value) {
    const oldValue = this.textContent,
      removedNodes = [...this.childNodes];
    __webcliTextSet(this.__id, value == null ? '' : String(value));
    __notify(this, [3, 4, 7, 8].includes(this.nodeType) ? 'characterData' : 'childList', {
      oldValue,
      removedNodes,
      addedNodes: [...this.childNodes]
    });
    if (this.tagName === 'SCRIPT') __dynamicScript(this);
  }
  get nodeValue() {
    return (this.nodeType === 3 || this.nodeType === 8) ? this.textContent : null;
  }
  set nodeValue(value) {
    if (this.nodeType === 3 || this.nodeType === 8) this.textContent = value;
  }
  get data() {
    return this.textContent;
  }
  set data(value) {
    this.textContent = value;
  }
  get innerText() {
    return this.textContent;
  }
  set innerText(value) {
    this.textContent = value;
  }
  get innerHTML() {
    return __webcliHTML(this.__id);
  }
  set innerHTML(value) {
    const removedNodes = [...this.childNodes];
    __webcliHTML(this.__id, String(value));
    for (const script of this.querySelectorAll('script')) script._executed = true;
    __notify(this, 'childList', {
      removedNodes,
      addedNodes: [...this.childNodes]
    });
  }
}

function __installDomMembers(type) {
  const descriptors = Object.getOwnPropertyDescriptors(type.prototype);
  delete descriptors.constructor;
  Object.defineProperties(DomNode.prototype, descriptors);
}
