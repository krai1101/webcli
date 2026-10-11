// Detached HTML documents reuse the native parser and document-owned factories.
{
  const token = {}, implementations = new WeakMap(), owners = new WeakMap();
  class DOMImplementation {
    constructor(secret, owner) {
      if (secret !== token) throw new TypeError('Illegal constructor');
      owners.set(this, owner);
    }
    createHTMLDocument(title) {
      if (!owners.has(this)) throw new TypeError('DOMImplementation required');
      const parsed = new DOMParser().parseFromString(
          '<!doctype html><html><head></head><body></body></html>', 'text/html');
      Object.defineProperty(parsed, 'URL', {value: 'about:blank', configurable: true});
      if (arguments.length) {
        if (typeof title === 'symbol') throw new TypeError('Cannot convert Symbol to string');
        const node = parsed.createElement('title');
        node.textContent = String(title);
        parsed.head.appendChild(node);
      }
      return parsed;
    }
  }
  Object.defineProperty(Document.prototype, 'implementation', {
    configurable: true, enumerable: true,
    get() {
      if (!(this instanceof Document)) throw new TypeError('Document required');
      if (!implementations.has(this)) implementations.set(this, new DOMImplementation(token, this));
      return implementations.get(this);
    }
  });
  globalThis.DOMImplementation = DOMImplementation;
}
