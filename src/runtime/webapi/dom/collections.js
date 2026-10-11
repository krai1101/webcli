// Keep the interface brand separate from the backing array. Pages use these
// constructors to distinguish DOM collections from ordinary JavaScript arrays.
const __collectionReaders = new WeakMap();
function __collectionItems(collection) {
  const read = __collectionReaders.get(collection);
  if (!read) throw new TypeError('Illegal invocation');
  return read();
}
class NodeList {
  constructor() { throw new TypeError('Illegal constructor'); }
  get length() { return __collectionItems(this).length; }
  item(index) { return __collectionItems(this)[Number(index) >>> 0] ?? null; }
  forEach(callback, thisArg) {
    if (typeof callback !== 'function') throw new TypeError('Callback must be a function');
    const size = this.length;
    for (let index = 0; index < size; ++index)
      callback.call(thisArg, this[index], index, this);
  }
  *values() {
    for (let index = 0; index < this.length; ++index) yield this[index];
  }
  *keys() { for (let index = 0; index < this.length; ++index) yield index; }
  *entries() {
    for (let index = 0; index < this.length; ++index) yield [index, this[index]];
  }
  [Symbol.iterator]() { return this.values(); }
  get [Symbol.toStringTag]() { return 'NodeList'; }
}
class HTMLCollection {
  constructor() { throw new TypeError('Illegal constructor'); }
  get length() { return __collectionItems(this).length; }
  item(index) { return __collectionItems(this)[Number(index) >>> 0] ?? null; }
  namedItem(name) {
    name = String(name);
    if (!name) return null;
    const items = __collectionItems(this);
    return items.find(node => node.id === name) ||
      items.find(node => node.getAttribute('name') === name) || null;
  }
  *[Symbol.iterator]() {
    for (let index = 0; index < this.length; ++index) yield this[index];
  }
  get [Symbol.toStringTag]() { return 'HTMLCollection'; }
}
Object.assign(globalThis, {NodeList, HTMLCollection});

// Live views retain DOM order; snapshots keep their original wrappers.
function __liveCollection(read, Interface = HTMLCollection) {
  let revision = -1,
    items = [];
  const current = () => {
    const nextRevision = __webcliDOMRevision();
    if (revision !== nextRevision) {
      items = read();
      revision = nextRevision;
    }
    return items;
  };
  const collection = new Proxy(Object.create(Interface.prototype), {
    get(target, key, receiver) {
      const items = current();
      if (key in target) return Reflect.get(target, key, receiver);
      if (Interface === HTMLCollection && typeof key === 'string' &&
          !(key in items) && !/^\d+$/.test(key))
        return items.find(node => node.id === key) || items.find(node => node.getAttribute(
          'name') === key);
      const value = Reflect.get(items, key);
      return typeof value === 'function' ? value.bind(items) : value;
    },
    has(target, key) {
      return key in current() || key in target;
    },
    ownKeys(target) {
      return [...current().keys()].map(String).concat(Reflect.ownKeys(target));
    },
    getOwnPropertyDescriptor(target, key) {
      if (typeof key === 'string' && /^\d+$/.test(key) && Number(key) < current().length)
        return {
          value: current()[Number(key)],
          enumerable: true,
          configurable: true,
          writable: false
        };
      return Reflect.getOwnPropertyDescriptor(target, key);
    },
    set(target, key, value, receiver) {
      if (key === 'length' || typeof key === 'string' && /^(0|[1-9]\d*)$/.test(key))
        return false;
      return Reflect.set(target, key, value, receiver);
    },
    defineProperty(target, key, descriptor) {
      if (key === 'length' || typeof key === 'string' && /^(0|[1-9]\d*)$/.test(key))
        return false;
      return Reflect.defineProperty(target, key, descriptor);
    },
    deleteProperty(target, key) {
      if (typeof key === 'string' && /^(0|[1-9]\d*)$/.test(key)) return false;
      return Reflect.deleteProperty(target, key);
    }
  });
  __collectionReaders.set(collection, current);
  return collection;
}
const __querySelectorAll = DomNode.prototype.querySelectorAll;
DomNode.prototype.querySelectorAll = function(selector) {
  const nodes = __querySelectorAll.call(this, selector);
  return __liveCollection(() => nodes, NodeList);
};
const __formsCollections = new WeakMap();
const __documentForms = new WeakMap();
Document.prototype.getElementsByName = function(name) {
  name = String(name);
  return __liveCollection(() => this.querySelectorAll('[name]').filter(node => node.getAttribute(
    'name') === name), NodeList);
};

function __formElements(form) {
  if (!__formsCollections.has(form)) __formsCollections.set(form, __liveCollection(() =>
    form.getRootNode().querySelectorAll('input,textarea,select,button').filter(node => node
      .form === form)));
  return __formsCollections.get(form);
}
Object.defineProperty(Document.prototype, 'forms', {
  get() {
    if (!__documentForms.has(this)) __documentForms.set(this, __liveCollection(() => this
      .querySelectorAll('form')));
    return __documentForms.get(this);
  }
});
Object.defineProperty(DomNode.prototype, 'baseURI', {
  get() {
    const owner = this.nodeType === 9 ? this : this.ownerDocument;
    const base = owner?.querySelector('base[href]');
    const current = owner?.URL || location.href;
    const url = current === 'about:blank' || current === 'about:srcdoc' ? __webcliInheritedBase : current;
    if (base) try {
      return __parseURL(base.getAttribute('href'), url).href;
    } catch {}
    return url;
  }
});
Object.defineProperty(HTMLFormElement.prototype, 'length', {
  get() {
    return this.elements.length;
  }
});
Document.prototype.importNode = document.importNode;
delete document.importNode;
Object.defineProperty(Document.prototype, 'scrollingElement', {
  configurable: true, get() { return this.documentElement; }
});
Document.prototype.adoptNode = function(node) {
  if (!(node instanceof DomNode)) throw new TypeError('adoptNode requires a Node');
  if (node.nodeType === 9 || node instanceof ShadowRoot)
    throw new DOMException('This node cannot be adopted', 'NotSupportedError');
  const oldOwner = node.ownerDocument, oldParent = node.parentNode;
  const index = oldParent ? oldParent.childNodes.indexOf(node) : -1;
  __webcliAdopt(this.__id, node.__id);
  __refreshNodeOwners();
  if (oldParent) {
    __notify(oldParent, 'childList', {removedNodes: [node]}, {index});
    __connectComponents(node);
  }
  if (oldOwner !== this)
    for (const child of __componentNodes(node))
      if (child._ceState === 'custom') __componentCall(child, 'adoptedCallback', oldOwner, this);
  return node;
};

// querySelectorAll is a static snapshot; legacy element collections are live.
DomNode.prototype.getElementsByTagName = function(tag) {
  tag = String(tag);
  return __liveCollection(() => this.querySelectorAll('*').filter(node => tag === '*' ||
    (node.namespaceURI === __htmlNamespace ? node.localName === tag.toLowerCase() : node
      .localName === tag)));
};
DomNode.prototype.getElementsByClassName = function(value) {
  const names = String(value).trim().split(/\s+/).filter(Boolean);
  return __liveCollection(() => names.length ? this.querySelectorAll('*').filter(node => names
    .every(name => node.classList.contains(name))) : []);
};
delete document.getElementsByTagName;
delete document.getElementsByClassName;

// HTMLDocument.scripts is a stable live view, including inert/data scripts.
const __documentScripts = new WeakMap();
Object.defineProperty(Document.prototype, 'scripts', {
  get() {
    if (!__documentScripts.has(this))
      __documentScripts.set(this, this.getElementsByTagName('script'));
    return __documentScripts.get(this);
  },
  configurable: true
});

const __childrenViews = new WeakMap(),
  __childNodeViews = new WeakMap();
for (const [name, cache, elements] of [
    ['children', __childrenViews, true],
    ['childNodes', __childNodeViews, false]
  ]) {
  Object.defineProperty(DomNode.prototype, name, {
    get() {
      if (!cache.has(this)) cache.set(this, __liveCollection(() => {
        const nodes = __webcliChildren(this.__id).map(__wrap);
        return elements ? nodes.filter(node => node.nodeType === 1) : nodes;
      }, elements ? HTMLCollection : NodeList));
      return cache.get(this);
    }
  });
}
DomNode.prototype.getElementsByTagNameNS = function(namespace, name) {
  namespace = namespace == null || namespace === '' ? null : String(namespace);
  name = String(name);
  return __liveCollection(() => this.querySelectorAll('*').filter(node => (namespace === '*' ||
    node.namespaceURI === namespace) && (name === '*' || node.localName === name)));
};
const __optionViews = new WeakMap();
Object.defineProperty(DomNode.prototype, 'options', {
  get() {
    if (this.tagName !== 'SELECT') return undefined;
    if (!__optionViews.has(this)) __optionViews.set(this, __liveCollection(() => this
      .querySelectorAll('option')));
    return __optionViews.get(this);
  }
});
Document.prototype.createEvent = function(type) {
  if (!(this instanceof Document)) throw new TypeError('Document required');
  if (!arguments.length) throw new TypeError('Event interface required');
  const name = String(type).toLowerCase();
  if (['event', 'events', 'htmlevents', 'svgevents'].includes(name)) return new Event('');
  if (name === 'customevent') return new CustomEvent('', {detail: null});
  throw new DOMException('Unsupported legacy event interface', 'NotSupportedError');
};
for (const name of ['Node', 'Element', 'Document', 'DocumentFragment', 'Text',
    'Comment', 'CharacterData', 'ShadowRoot', 'HTMLIFrameElement']) {
  const prototype = globalThis[name]?.prototype;
  if (prototype && !Object.prototype.hasOwnProperty.call(prototype, Symbol.toStringTag))
    Object.defineProperty(prototype, Symbol.toStringTag, {value: name, configurable: true});
}
