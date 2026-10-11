// Nonvisual component lifecycle and shadow-tree APIs.
const __definitions = new Map();
const __constructors = new WeakMap();
const __definitionWaiters = new Map();
const __constructionStack = [];
const __shadowRoots = new WeakMap();

function __componentError(error) {
  __webcliReportError(String(error) + (error?.stack ? '\n' + error.stack : ''));
}
// Reactions drain after the outer DOM operation; reentrant reactions queue.
const __componentReactions = [];
let __reactionDepth = 0,
  __drainingReactions = false;

function __flushReactions() {
  if (__reactionDepth || __drainingReactions) return;
  __drainingReactions = true;
  try {
    let count = 0;
    while (__componentReactions.length) {
      if (++count > 10000) {
        __componentReactions.length = 0;
        throw new Error('Custom element reaction quota exceeded');
      }
      const {
        node,
        name,
        args
      } = __componentReactions.shift();
      try {
        if (typeof node[name] === 'function') node[name](...args);
      } catch (error) {
        __componentError(error);
      }
    }
  } finally {
    __drainingReactions = false;
  }
}

function __withReactions(action) {
  ++__reactionDepth;
  try {
    return action();
  } finally {
    --__reactionDepth;
    __flushReactions();
  }
}

function __componentCall(node, name, ...args) {
  __componentReactions.push({
    node,
    name,
    args
  });
  __flushReactions();
}
class NativeHTMLElement extends Element {
  constructor() {
    const pending = __constructionStack[__constructionStack.length - 1];
    if (pending && pending.constructor === new.target) return pending.node;
    const definition = __constructors.get(new.target);
    if (!definition) throw new TypeError('Illegal HTMLElement constructor');
    const node = __wrap(__webcliCreateElement(definition.name));
    Object.setPrototypeOf(node, new.target.prototype);
    node._ceDefinition = definition;
    node._ceState = 'constructing';
    __queueMicrotask(() => {
      if (node._ceState === 'constructing') {
        node._ceState = 'custom';
        __connectComponents(node);
      }
    });
    return node;
  }
}
globalThis.HTMLElement = NativeHTMLElement;
for (const constructor of [globalThis.HTMLDialogElement, globalThis.HTMLIFrameElement])
  if (constructor) Object.setPrototypeOf(constructor.prototype, HTMLElement.prototype);
for (const node of __cachedNodes())
  if (node.nodeType === 1 && node.namespaceURI === __htmlNamespace &&
      [DomNode.prototype, Element.prototype].includes(Object.getPrototypeOf(node)))
    Object.setPrototypeOf(node, HTMLElement.prototype);

function __componentNodes(root) {
  const nodes = [],
    pending = [root];
  while (pending.length) {
    const node = pending.pop();
    nodes.push(node);
    if (nodes.length > 50000) throw new Error('component traversal quota exceeded');
    const children = [...node.childNodes];
    const shadow = __shadowRoots.get(node);
    if (shadow) children.push(shadow);
    for (let i = children.length - 1; i >= 0; --i) pending.push(children[i]);
  }
  return nodes;
}

function __upgradeComponent(node) {
  if (node.nodeType !== 1 || node.namespaceURI !== __htmlNamespace || node._ceState) return;
  const definition = __definitions.get(node.tagName.toLowerCase());
  if (!definition) return;
  node._ceState = 'constructing';
  node._ceDefinition = definition;
  Object.setPrototypeOf(node, definition.constructor.prototype);
  __constructionStack.push({
    node,
    constructor: definition.constructor
  });
  try {
    const constructed = Reflect.construct(definition.constructor, []);
    if (constructed !== node) throw new TypeError(
      'custom element constructor must return its element');
    node._ceState = 'custom';
    for (const name of definition.attributes)
      if (node.hasAttribute(name))
        __componentCall(node, 'attributeChangedCallback', name, null, node.getAttribute(name));
  } catch (error) {
    node._ceState = 'failed';
    __componentError(error);
  } finally {
    __constructionStack.pop();
  }
}

function __connectComponents(root) {
  for (const node of __componentNodes(root)) {
    __upgradeComponent(node);
    if (node._ceState !== 'custom') continue;
    const connected = node.isConnected;
    if (connected !== !!node._ceConnected) {
      node._ceConnected = connected;
      __componentCall(node, connected ? 'connectedCallback' : 'disconnectedCallback');
    }
  }
}

function __customName(name) {
  name = String(name);
  if (!/^[a-z][a-z0-9._-]*-[a-z0-9._-]*$/.test(name) || ['annotation-xml', 'color-profile',
      'font-face', 'font-face-src', 'font-face-uri', 'font-face-format', 'font-face-name',
      'missing-glyph'
    ].includes(name))
    throw new SyntaxError('invalid custom element name');
  return name;
}
class CustomElementRegistry {
  define(name, constructor, options) {
    name = __customName(name);
    if (options?.extends) throw new Error('customized built-in elements are unsupported');
    if (typeof constructor !== 'function' || !(constructor.prototype instanceof HTMLElement))
      throw new TypeError('HTMLElement subclass required');
    if (__definitions.has(name) || __constructors.has(constructor)) throw new DOMException(
      'custom element already defined: ' + name, 'NotSupportedError');
    if (__definitions.size >= 256) throw new Error('custom element definition quota exceeded');
    const attributes = [...(constructor.observedAttributes || [])].map(String);
    const definition = {
      name,
      constructor,
      attributes
    };
    __definitions.set(name, definition);
    __constructors.set(constructor, definition);
    const roots = [document, ...[...__cachedNodes()].map(node => __shadowRoots.get(node))
      .filter(Boolean)
    ];
    for (const root of roots)
      for (const node of root.querySelectorAll(name)) __connectComponents(node);
    const resolve = __definitionWaiters.get(name)?.resolve;
    if (resolve) {
      __definitionWaiters.delete(name);
      resolve(constructor);
    }
  }
  get(name) {
    return __definitions.get(String(name))?.constructor;
  }
  getName(constructor) {
    return __constructors.get(constructor)?.name ?? null;
  }
  whenDefined(name) {
    try {
      name = __customName(name);
    } catch (error) {
      return Promise.reject(error);
    }
    if (__definitions.has(name)) return Promise.resolve(this.get(name));
    if (!__definitionWaiters.has(name)) {
      if (__definitionWaiters.size >= 256) return Promise.reject(new Error(
        'custom element waiter quota exceeded'));
      let resolve;
      const promise = new Promise(accept => {
        resolve = accept;
      });
      __definitionWaiters.set(name, {
        promise,
        resolve
      });
    }
    return __definitionWaiters.get(name).promise;
  }
  upgrade(root) {
    for (const node of __componentNodes(root)) __upgradeComponent(node);
    __connectComponents(root);
  }
}
globalThis.CustomElementRegistry = CustomElementRegistry;
globalThis.customElements = new CustomElementRegistry();
const __createElement = document.createElement;
document.createElement = function(name) {
  return __withReactions(() => {
    const node = __createElement(String(name));
    __upgradeComponent(node);
    return node;
  });
};
for (const method of ['appendChild', 'insertBefore', 'removeChild']) {
  const original = DomNode.prototype[method];
  DomNode.prototype[method] = function(...args) {
    return __withReactions(() => {
      const affected = args[0].nodeType === 11 ? [...args[0].childNodes] : [args[0]];
      const result = original.apply(this, args);
      for (const node of affected) __connectComponents(node);
      return result;
    });
  };
}
for (const name of ['innerHTML', 'textContent']) {
  const descriptor = Object.getOwnPropertyDescriptor(DomNode.prototype, name);
  Object.defineProperty(DomNode.prototype, name, {
    ...descriptor,
    set(value) {
      __withReactions(() => {
        const removed = [...this.childNodes];
        descriptor.set.call(this, value);
        for (const node of removed) __connectComponents(node);
        __connectComponents(this);
      });
    }
  });
}
for (const name of ['setAttribute', 'removeAttribute']) {
  const original = DomNode.prototype[name];
  DomNode.prototype[name] = function(attribute, ...args) {
    attribute = String(attribute);
    if (this.namespaceURI === __htmlNamespace) attribute = attribute.toLowerCase();
    const before = this.getAttribute(attribute);
    return __withReactions(() => {
      const result = original.call(this, attribute, ...args);
      const after = this.getAttribute(attribute);
      if (before !== after && this._ceState === 'custom' && this._ceDefinition.attributes
        .includes(attribute))
        __componentCall(this, 'attributeChangedCallback', attribute, before, after);
      return result;
    });
  };
}
Object.defineProperty(DomNode.prototype, 'content', {
  get() {
    return this.tagName === 'TEMPLATE' ? __wrap(__webcliTemplateContent(this.__id)) :
      undefined;
  }
});
Object.defineProperty(DomNode.prototype, 'shadowRoot', {
  get() {
    const root = __shadowRoots.get(this);
    return root?.mode === 'open' ? root : null;
  }
});
const __shadowHostTags = new Set(
  'ARTICLE ASIDE BLOCKQUOTE BODY DIV FOOTER H1 H2 H3 H4 H5 H6 HEADER MAIN NAV P SECTION SPAN'
  .split(' '));

function __canHostShadow(node) {
  return node.nodeType === 1 && (__shadowHostTags.has(node.tagName) || node.tagName.includes('-'));
}
DomNode.prototype.attachShadow = function(options) {
  if (!__canHostShadow(this) || !options || !['open', 'closed'].includes(options.mode))
    throw new TypeError('shadow mode required');
  const existing = __shadowRoots.get(this);
  if (existing?._declarative && existing.mode === options.mode) {
    existing._declarative = false;
    existing.textContent = '';
    return existing;
  }
  const id = __webcliAttachShadow(this.__id);
  const root = new ShadowRoot(id, this, options);
  __cacheNode(id, root);
  __shadowRoots.set(this, root);
  return root;
};
DomNode.prototype.getRootNode = function(options = {}) {
  let node = this;
  while (node.parentNode) node = node.parentNode;
  return options.composed && node instanceof ShadowRoot ? node.host.getRootNode(options) : node;
};
Object.defineProperty(DomNode.prototype, 'isConnected', {
  get() {
    return this.getRootNode({
      composed: true
    }) === document;
  }
});
class ShadowRoot extends DomNode {
  constructor(id, host, options) {
    super(id);
    Object.defineProperties(this, {
      host: {
        value: host
      },
      mode: {
        value: options.mode
      },
      delegatesFocus: {
        value: !!options.delegatesFocus
      }
    });
  }
  get parentNode() {
    return null;
  }
  get activeElement() {
    return this.contains(document.activeElement) ? document.activeElement : null;
  }
  getElementById(id) {
    return this.querySelectorAll('[id]').find(node => node.id === String(id)) || null;
  }
}
globalThis.ShadowRoot = ShadowRoot;
class Document extends DomNode {}
globalThis.Document = Document;
Object.defineProperty(Document.prototype, 'referrer', {
  configurable: true, enumerable: true,
  get() { return __webcliDocumentReferrer(this.__id); }
});
Object.setPrototypeOf(document, Document.prototype);
class DocumentFragment extends DomNode {
  constructor() {
    return document.createDocumentFragment();
  }
  static[Symbol.hasInstance](node) {
    return node instanceof DomNode && node.nodeType === 11;
  }
}
globalThis.DocumentFragment = DocumentFragment;
class HTMLSlotElement extends HTMLElement {
  static[Symbol.hasInstance](node) {
    return node instanceof DomNode && node.tagName === 'SLOT';
  }
}

function __assignedSlot(node) {
  const root = __shadowRoots.get(node.parentNode);
  if (!root) return null;
  const name = node.nodeType === 1 ? node.getAttribute('slot') || '' : '';
  return root.querySelectorAll('slot').find(slot => slot.name === name) || null;
}
Object.defineProperty(DomNode.prototype, 'assignedSlot', {
  get() {
    const slot = __assignedSlot(this);
    return slot?.getRootNode().mode === 'closed' ? null : slot;
  }
});
HTMLSlotElement.prototype.assignedNodes = function(options = {}) {
  if (this.tagName !== 'SLOT') throw new TypeError('slot required');
  const root = this.getRootNode();
  let nodes = root instanceof ShadowRoot ? root.host.childNodes.filter(node =>
    (node.nodeType === 1 || node.nodeType === 3) && __assignedSlot(node) === this) : [];
  if (options.flatten) {
    if (!nodes.length) nodes = this.childNodes;
    nodes = nodes.flatMap(node => node.tagName === 'SLOT' ? node.assignedNodes({
      flatten: true
    }) : [node]);
  }
  return nodes;
};
HTMLSlotElement.prototype.assignedElements = function(options) {
  return this.assignedNodes(options).filter(node => node.nodeType === 1);
};
globalThis.HTMLSlotElement = HTMLSlotElement;

// Coalesce assignment changes at the microtask checkpoint, independently of
// MutationObserver subscriptions installed by page code.
function __slotMutation(target, type, extra) {
  if (type !== 'childList' && !(type === 'attributes' && ['slot', 'name'].includes(extra
      .attributeName))) return;
  const tree = target.getRootNode?.();
  const roots = new Set([__shadowRoots.get(target), __shadowRoots.get(target.parentNode),
    tree instanceof ShadowRoot ? tree : null
  ]);
  for (const root of roots) {
    if (!root || root._slotQueued) continue;
    root._slotQueued = true;
    __queueMicrotask(() => {
      root._slotQueued = false;
      const previous = root._slotAssignments || new Map(),
        next = new Map();
      for (const slot of root.querySelectorAll('slot')) {
        const assigned = slot.assignedNodes();
        next.set(slot, assigned);
        const old = previous.get(slot) || [];
        if (old.length !== assigned.length || old.some((node, index) => node !== assigned[
            index]))
          slot.dispatchEvent(new Event('slotchange', {
            bubbles: true
          }));
      }
      root._slotAssignments = next;
    });
  }
}
const __cloneNode = DomNode.prototype.cloneNode;
DomNode.prototype.cloneNode = function(deep) {
  const clone = __cloneNode.call(this, deep);
  __connectComponents(clone);
  return clone;
};

// Declarative shadow markup belongs to the component tree before scripts run.
// innerHTML-created templates remain inert, as in the ordinary HTML setter.
function __adoptDeclarativeTemplates(scope) {
  const previous = globalThis.__webcliInitializingDOM;
  globalThis.__webcliInitializingDOM = true;
  try {
    const roots = [scope];
    while (roots.length) {
      const current = roots.pop();
      const templates = current.tagName === 'TEMPLATE' ? [current] :
          current.querySelectorAll('template[shadowrootmode]');
      for (const template of templates) {
        const host = template.parentElement, mode = template.getAttribute('shadowrootmode');
        if (!host || !__canHostShadow(host) || __shadowRoots.has(host) ||
            !['open', 'closed'].includes(mode)) continue;
        const root = host.attachShadow({
          mode, delegatesFocus: template.hasAttribute('shadowrootdelegatesfocus')
        });
        root.appendChild(template.content);
        template.remove();
        root._declarative = true;
        roots.push(root);
      }
    }
  } finally {
    globalThis.__webcliInitializingDOM = previous;
  }
}

for (const method of ['append', 'prepend', 'replaceChild', 'replaceChildren']) {
  const original = DomNode.prototype[method];
  DomNode.prototype[method] = function(...args) {
    return __withReactions(() => original.apply(this, args));
  };
}
for (const method of ['define', 'upgrade']) {
  const original = CustomElementRegistry.prototype[method];
  CustomElementRegistry.prototype[method] = function(...args) {
    return __withReactions(() => original.apply(this, args));
  };
}
