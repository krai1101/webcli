// WebCLI has no browser plugins or PDF renderer. Expose the real empty list
// for document feature detection, rather than inventing installed plugins.
(() => {
  const token = Symbol('plugin array'), instances = new WeakSet();
  const requireArray = value => {
    if (!instances.has(value)) throw new TypeError('PluginArray required');
  };
  class PluginArray {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal PluginArray constructor');
      instances.add(this);
    }
    get length() { requireArray(this); return 0; }
    item(index) {
      requireArray(this);
      if (!arguments.length) throw new TypeError('Plugin index required');
      Number(index);
      return null;
    }
    namedItem(name) {
      requireArray(this);
      if (!arguments.length) throw new TypeError('Plugin name required');
      String(name);
      return null;
    }
    refresh() { requireArray(this); }
    *[Symbol.iterator]() { requireArray(this); }
    get [Symbol.toStringTag]() { return 'PluginArray'; }
  }
  globalThis.PluginArray = PluginArray;
  Object.defineProperty(navigator, 'plugins', {enumerable: true, configurable: true,
    value: new PluginArray(token)});
})();
