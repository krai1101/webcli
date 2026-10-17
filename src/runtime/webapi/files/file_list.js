// File collections are in-memory DOM interfaces, not local filesystem access.
const __fileListEngine = (() => {
  const entries = new WeakMap(), token = Symbol('FileList constructor');
  const values = object => {
    const files = entries.get(object);
    if (!files) throw new TypeError('Illegal invocation');
    return files;
  };
  globalThis.FileList = class FileList {
    constructor(key) {
      if (key !== token) throw new TypeError('Illegal constructor');
      entries.set(this, []);
    }
    get length() { return values(this).length; }
    item(index) { return values(this)[Number(index) >>> 0] ?? null; }
    [Symbol.iterator]() { return values(this).values(); }
    get [Symbol.toStringTag]() { return 'FileList'; }
  };
  return Object.freeze({create: () => new FileList(token), has: value => entries.has(value)});
})();
