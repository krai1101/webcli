// DOM editing helpers shared by component libraries and ordinary pages.
DomNode.prototype.hasAttributes = function() {
  return this.attributes.length !== 0;
};
DomNode.prototype.getAttributeNames = function() {
  return this.attributes.map(attribute => attribute.name);
};
Object.defineProperty(DomNode.prototype, 'lang', {
  get() {
    return this.getAttribute('lang') || '';
  },
  set(value) {
    this.setAttribute('lang', value);
  }
});

function __coerceNode(value) {
  return value instanceof DomNode ? value : document.createTextNode(String(value));
}
DomNode.prototype.before = function(...nodes) {
  const parent = this.parentNode;
  if (!parent) return;
  for (const node of nodes.map(__coerceNode)) parent.insertBefore(node, this);
};
DomNode.prototype.after = function(...nodes) {
  const parent = this.parentNode,
    before = this.nextSibling;
  if (!parent) return;
  for (const node of nodes.map(__coerceNode)) parent.insertBefore(node, before);
};
DomNode.prototype.replaceWith = function(...values) {
  const parent = this.parentNode;
  if (!parent) return;
  const nodes = values.map(__coerceNode),
    marker = document.createComment('');
  parent.insertBefore(marker, this);
  parent.removeChild(this);
  for (const node of nodes) parent.insertBefore(node, marker);
  parent.removeChild(marker);
};
DomNode.prototype.insertAdjacentElement = function(position, element) {
  if (!(element instanceof DomNode) || element.nodeType !== 1) throw new TypeError(
    'element required');
  const where = String(position).toLowerCase();
  if (where === 'beforebegin') {
    if (!this.parentNode) return null;
    this.parentNode.insertBefore(element, this);
  } else if (where === 'afterbegin') this.insertBefore(element, this.firstChild);
  else if (where === 'beforeend') this.appendChild(element);
  else if (where === 'afterend') {
    if (!this.parentNode) return null;
    this.parentNode.insertBefore(element, this.nextSibling);
  } else throw new SyntaxError('invalid insertion position');
  return element;
};
DomNode.prototype.insertAdjacentHTML = function(position, html) {
  const where = String(position).toLowerCase();
  if (!['beforebegin', 'afterbegin', 'beforeend', 'afterend'].includes(where))
  throw new SyntaxError('invalid insertion position');
  const outside = where === 'beforebegin' || where === 'afterend';
  const parent = outside ? this.parentNode : this;
  if (!parent || parent.nodeType === 9) throw new Error('insertion requires a parent element');
  const context = outside ? parent : this;
  const container = __wrap(__webcliCreateElement(context.tagName || 'div'));
  container.innerHTML = String(html);
  const before = where === 'beforebegin' ? this : where === 'afterbegin' ? this.firstChild :
    where === 'afterend' ? this.nextSibling : null;
  for (const node of [...(container.content || container).childNodes]) parent.insertBefore(node,
    before);
};
DomNode.prototype.insertAdjacentText = function(position, text) {
  const where = String(position).toLowerCase(),
    node = document.createTextNode(String(text));
  if (where === 'beforebegin') {
    if (this.parentNode) this.parentNode.insertBefore(node, this);
  } else if (where === 'afterbegin') this.insertBefore(node, this.firstChild);
  else if (where === 'beforeend') this.appendChild(node);
  else if (where === 'afterend') {
    if (this.parentNode) this.parentNode.insertBefore(node, this.nextSibling);
  } else throw new SyntaxError('invalid insertion position');
};
DomNode.prototype.scrollTo = function(first = 0, second = 0) {
  const options = first !== null && typeof first === 'object' ? first : {
    left: first,
    top: second
  };
  const coordinate = value => Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0;
  const oldTop = this.scrollTop || 0,
    oldLeft = this.scrollLeft || 0;
  this.scrollTop = coordinate(options.top ?? oldTop);
  this.scrollLeft = coordinate(options.left ?? oldLeft);
  if (this.scrollTop !== oldTop || this.scrollLeft !== oldLeft) queueMicrotask(() => this
    .dispatchEvent(new Event('scroll')));
};
DomNode.prototype.scroll = DomNode.prototype.scrollTo;
// No visual layout is synthesized: use the existing geometry/scroll state.
DomNode.prototype.scrollIntoView = function() {
  const view = this.ownerDocument?.defaultView;
  if (!view) return;
  const rect = this.getBoundingClientRect();
  view.scrollTo({left: view.scrollX + rect.left, top: view.scrollY + rect.top});
};
DomNode.prototype.scrollTop = 0;
DomNode.prototype.scrollLeft = 0;
for (const [name, tag] of [
    ['HTMLAnchorElement', 'A'],
    ['HTMLFormElement', 'FORM'],
    ['HTMLInputElement', 'INPUT'],
    ['HTMLLabelElement', 'LABEL'],
    ['HTMLButtonElement', 'BUTTON'],
    ['HTMLSelectElement', 'SELECT'],
    ['HTMLTextAreaElement', 'TEXTAREA'],
    ['HTMLTemplateElement', 'TEMPLATE'],
    ['HTMLOptionElement', 'OPTION'],
    ['HTMLOptGroupElement', 'OPTGROUP'],
    ['HTMLScriptElement', 'SCRIPT'],
    ['HTMLMetaElement', 'META'],
    ['HTMLLinkElement', 'LINK'],
    ['HTMLStyleElement', 'STYLE'],
    ['HTMLImageElement', 'IMG'],
    ['HTMLDataElement', 'DATA'],
    ['HTMLDivElement', 'DIV'],
    ['HTMLSpanElement', 'SPAN'],
    ['HTMLPreElement', 'PRE'],
    ['HTMLUListElement', 'UL'],
    ['HTMLOListElement', 'OL'],
    ['HTMLLIElement', 'LI']
  ]) {
  globalThis[name] = class extends HTMLElement {
    static[Symbol.hasInstance](node) {
      return node instanceof DomNode && node.namespaceURI === __htmlNamespace && node
        .tagName === tag;
    }
  };
  __elementTypes.set(tag, globalThis[name]);
  Object.defineProperty(globalThis[name].prototype, Symbol.toStringTag, {
    value: name, configurable: true
  });
}
for (const name of ['HTMLUListElement', 'HTMLOListElement']) {
  Object.defineProperty(globalThis[name].prototype, 'compact', {
    get() { return this.hasAttribute('compact'); },
    set(value) { this.toggleAttribute('compact', !!value); }
  });
}
for (const [name, property, fallback] of [
    ['HTMLOListElement', 'start', 1], ['HTMLLIElement', 'value', 0]]) {
  Object.defineProperty(globalThis[name].prototype, property, {
    get() {
      const value = this.getAttribute(property);
      return value !== null && /^[\t\n\f\r ]*[+-]?\d+/.test(value) ?
        Number.parseInt(value, 10) | 0 : fallback;
    },
    set(value) { this.setAttribute(property, String(Number(value) | 0)); }
  });
}
Object.defineProperty(HTMLOListElement.prototype, 'reversed', {
  get() { return this.hasAttribute('reversed'); },
  set(value) { this.toggleAttribute('reversed', !!value); }
});

globalThis.HTMLHeadingElement = class extends HTMLElement {
  static[Symbol.hasInstance](node) {
    return node instanceof DomNode && node.namespaceURI === __htmlNamespace && /^H[1-6]$/.test(
      node.tagName);
  }
};
for (let level = 1; level <= 6; ++level) __elementTypes.set('H' + level, HTMLHeadingElement);

// Component frameworks test this hierarchy even when a page plays no media.
// These are DOM element types; they do not provide decoding or playback.
globalThis.HTMLMediaElement = class HTMLMediaElement extends HTMLElement {
  static [Symbol.hasInstance](node) {
    return node instanceof DomNode && node.namespaceURI === __htmlNamespace &&
      (node.tagName === 'AUDIO' || node.tagName === 'VIDEO');
  }
};
for (const [name, tag] of [['HTMLAudioElement', 'AUDIO'], ['HTMLVideoElement', 'VIDEO']]) {
  globalThis[name] = class extends HTMLMediaElement {
    static [Symbol.hasInstance](node) {
      return node instanceof DomNode && node.namespaceURI === __htmlNamespace && node.tagName === tag;
    }
  };
  __elementTypes.set(tag, globalThis[name]);
}

// Expose native accessor descriptors where controlled input frameworks expect them.
for (const name of ['HTMLInputElement', 'HTMLTextAreaElement', 'HTMLSelectElement']) {
  for (const property of ['value', 'checked', 'selectedIndex']) {
    const descriptor = Object.getOwnPropertyDescriptor(DomNode.prototype, property);
    if (descriptor) Object.defineProperty(globalThis[name].prototype, property, descriptor);
  }
}

for (const [tag, constructor] of [
    ['DIALOG', HTMLDialogElement],
    ['CANVAS', HTMLCanvasElement],
    ['IFRAME', HTMLIFrameElement],
    ['SLOT', HTMLSlotElement]
  ]) __elementTypes.set(tag, constructor);
// Parsed component content can already have wrappers before typed APIs install.
for (const node of __cachedNodes()) {
  if (node.nodeType === 1 && Object.getPrototypeOf(node) === HTMLElement.prototype && __elementTypes
    .has(node.tagName))
    Object.setPrototypeOf(node, __elementTypes.get(node.tagName).prototype);
}
DomNode.prototype.isEqualNode = function(other) {
  if (!(other instanceof DomNode)) return false;
  const pending = [
    [this, other]
  ];
  while (pending.length) {
    const [first, second] = pending.pop();
    if (first === second) continue;
    if (first.nodeType !== second.nodeType || first.nodeName !== second.nodeName ||
      first.namespaceURI !== second.namespaceURI || first.nodeValue !== second.nodeValue)
    return false;
    if (first.nodeType === 1) {
      if (first.localName !== second.localName || first.prefix !== second.prefix) return false;
      const left = first.attributes,
        right = second.attributes;
      if (left.length !== right.length) return false;
      for (const attr of left)
        if (!right.some(value => value.namespaceURI === attr.namespaceURI && value.localName ===
            attr.localName && value.value === attr.value)) return false;
    }
    const left = first.childNodes,
      right = second.childNodes;
    if (left.length !== right.length) return false;
    for (let i = 0; i < left.length; ++i) pending.push([left[i], right[i]]);
  }
  return true;
};

Object.defineProperty(HTMLMetaElement.prototype, 'content', {
  get() {
    return this.getAttribute('content') || '';
  },
  set(value) {
    this.setAttribute('content', value);
  }
});
Object.defineProperty(HTMLScriptElement, 'supports', {
  value: function supports(type) {
    return ['classic', 'module', 'importmap'].includes(String(type));
  }
});
