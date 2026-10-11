// URL decomposition is used by client-side routers even when no visual layout
// is involved. Resolve through the document base, just like the href accessor.
for (const property
         of ['origin', 'protocol', 'username', 'password', 'host', 'hostname',
             'port', 'pathname', 'search', 'hash']) {
  const descriptor = {
    configurable: true,
    enumerable: true,
    get() {
      if (!this.hasAttribute('href')) return '';
      try {
        return __parseURL(this.href)[property];
      } catch {
        return '';
      }
    }
  };
  if (property !== 'origin')
    descriptor.set = function(value) {
      try {
        const url = __parseURL(this.href || this.baseURI, undefined, property, String(value));
        this.href = url.href;
      } catch {
      }
    };
  Object.defineProperty(HTMLAnchorElement.prototype, property, descriptor);
}
Object.defineProperty(Document.prototype, 'location', {
  configurable: true,
  get() {
    return this === document ? globalThis.location : null;
  },
  set(value) {
    if (this === document) globalThis.location.href = value;
  }
});
