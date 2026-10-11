__installDomMembers(class {
  getAttribute(name) {
    return __webcliAttrGet(this.__id, String(name));
  }
  get attributes() {
    // Only elements have an attribute collection. Generic node wrappers also
    // represent text, comments, documents, and attributes.
    if (this.nodeType !== 1) return undefined;
    const entries = __webcliAttributes(this.__id).map(entry => __attributeView(this, entry));
    const named = name => entries.find(attribute => attribute.name === String(name)) || null;
    Object.defineProperties(entries, {
      item: {
        value: index => entries[Number(index)] || null
      },
      getNamedItem: {
        value: named
      },
    });
    return new Proxy(entries, {
      get(target, key, receiver) {
        if (typeof key === 'string' && !(key in target)) return named(key) || undefined;
        return Reflect.get(target, key, receiver);
      },
    });
  }
  getAttributeNode(name) {
    return this.attributes.getNamedItem(name);
  }
  hasAttribute(name) {
    return this.getAttribute(name) !== null;
  }
  setAttribute(name, value) {
    name = String(name);
    const oldValue = this.getAttribute(name);
    __webcliAttrSet(this.__id, name, String(value));
    __notify(this, 'attributes', {
      attributeName: name,
      oldValue
    });
    if (name === 'src' && this.tagName === 'SCRIPT') __dynamicScript(this);
    if (name === 'src' && this.tagName === 'IMG') __loadImage(this);
  }
  removeAttribute(name) {
    name = String(name);
    const oldValue = this.getAttribute(name);
    if (oldValue !== null) {
      __detachAttribute(this, name, oldValue);
      __webcliAttrRemove(this.__id, name);
      __notify(this, 'attributes', {
        attributeName: name,
        oldValue
      });
      if (name === 'src' && this.tagName === 'SCRIPT') __dynamicScript(this);
      if (name === 'src' && this.tagName === 'IMG') __loadImage(this);
    }
  }
  toggleAttribute(name, force) {
    const yes = force === undefined ? !this.hasAttribute(name) : !!force;
    if (yes) this.setAttribute(name, '');
    else this.removeAttribute(name);
    return yes;
  }
  querySelectorAll(selector) {
    const result = __webcliQuery(this.__id, typeof __targetSelector === 'function' ?
      __targetSelector(selector, this) : String(selector)).map(__wrap);
    return this === document ? result : result.filter(n => n !== this);
  }
  querySelector(selector) {
    return this.querySelectorAll(selector)[0] || null;
  }
  getElementsByTagName(tag) {
    return this.querySelectorAll(String(tag));
  }
  getElementsByClassName(value) {
    return this.querySelectorAll('.' + String(value).trim().split(/\s+/).join('.'));
  }
  cloneNode(deep = false) {
    return __wrap(__webcliClone(this.__id, !!deep));
  }
  matches(selector) {
    return this.nodeType === 1 && __webcliQuery(this.__id, typeof __targetSelector ===
      'function' ? __targetSelector(selector, this) : String(selector), true);
  }
  closest(selector) {
    for (let n = this; n && n.nodeType === 1; n = n.parentElement)
      if (n.matches(selector)) return n;
    return null;
  }
  contains(node) {
    for (let n = node; n; n = n.parentNode)
      if (n === this) return true;
    return false;
  }
  appendChild(child) {
    for (let ancestor = this; ancestor; ancestor = ancestor.parentNode || (
        ancestor instanceof globalThis.ShadowRoot ? ancestor.host : null))
      if (ancestor === child) throw new DOMException('Cannot insert an ancestor',
        'HierarchyRequestError');
    if (!__webcliValidateInsert(this.__id, child.__id, -1))
      throw new DOMException('Invalid insertion', 'HierarchyRequestError');
    if (child.ownerDocument !== (this.nodeType === 9 ? this : this.ownerDocument))
      (this.nodeType === 9 ? this : this.ownerDocument).adoptNode(child);
    if (child.nodeType === 11) {
      for (const node of [...child.childNodes]) this.appendChild(node);
      return child;
    }
    const old = child.parentNode;
    if (old) old.removeChild(child);
    __webcliAppend(this.__id, child.__id);
    __notify(this, 'childList', {
      addedNodes: [child]
    });
    __connectedScripts(child);
    return child;
  }
  insertBefore(child, before) {
    if (!before) return this.appendChild(child);
    if (before.parentNode !== this) throw new DOMException('Not a child', 'NotFoundError');
    for (let ancestor = this; ancestor; ancestor = ancestor.parentNode || (
        ancestor instanceof globalThis.ShadowRoot ? ancestor.host : null))
      if (ancestor === child) throw new DOMException('Cannot insert an ancestor',
        'HierarchyRequestError');
    if (!__webcliValidateInsert(this.__id, child.__id, before.__id))
      throw new DOMException('Invalid insertion', 'HierarchyRequestError');
    if (child.ownerDocument !== (this.nodeType === 9 ? this : this.ownerDocument))
      (this.nodeType === 9 ? this : this.ownerDocument).adoptNode(child);
    if (child.nodeType === 11) {
      for (const node of [...child.childNodes]) this.insertBefore(node, before);
      return child;
    }
    if (child === before) return child;
    if (child.parentNode) child.parentNode.removeChild(child);
    __webcliInsert(this.__id, child.__id, before.__id);
    __notify(this, 'childList', {
      addedNodes: [child]
    });
    __connectedScripts(child);
    return child;
  }
  removeChild(child) {
    if (child.parentNode !== this) throw new DOMException('Not a child', 'NotFoundError');
    const index = this.childNodes.indexOf(child);
    __webcliRemove(child.__id);
    __notify(this, 'childList', {
      removedNodes: [child]
    }, {index});
    return child;
  }
  replaceChild(child, before) {
    if (child === before) return before;
    this.insertBefore(child, before);
    this.removeChild(before);
    return before;
  }
  remove() {
    if (this.parentNode) this.parentNode.removeChild(this);
  }
  append(...children) {
    for (const child of children) this.appendChild(typeof child === 'string' ? (this.nodeType === 9 ? this : this.ownerDocument)
      .createTextNode(child) : child);
  }
  prepend(...children) {
    for (const child of children.reverse()) this.insertBefore(typeof child === 'string' ? (this.nodeType === 9 ? this : this.ownerDocument)
      .createTextNode(child) : child, this.firstChild);
  }
  replaceChildren(...children) {
    while (this.firstChild) this.removeChild(this.firstChild);
    this.append(...children);
  }
});
