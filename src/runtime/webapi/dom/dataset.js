// A live named-property view; native attribute operations own mutation delivery.
globalThis.DOMStringMap = class DOMStringMap {
  constructor() { throw new TypeError('Illegal constructor'); }
};
Object.defineProperty(DOMStringMap.prototype, Symbol.toStringTag, {
  value: 'DOMStringMap', configurable: true
});
const __datasets = new WeakMap();
function __dataset(node) {
  if (__datasets.has(node)) return __datasets.get(node);
  const attribute = key => 'data-' + key.replace(/[A-Z]/g, c => '-' + c.toLowerCase());
  const names = () => node.attributes.map(a => a.name)
    .filter(name => name.startsWith('data-') && !/[A-Z]/.test(name))
    .map(name => name.slice(5).replace(/-([a-z])/g, (_, c) => c.toUpperCase()));
  const write = (key, value) => {
    if (/-[a-z]/.test(key)) throw new DOMException('Invalid dataset property name', 'SyntaxError');
    if (typeof value === 'symbol') throw new TypeError('Cannot convert a Symbol to a string');
    node.setAttribute(attribute(key), String(value));
    return true;
  };
  const map = new Proxy(Object.create(DOMStringMap.prototype), {
    get(target, key, receiver) {
      if (typeof key === 'string') {
        const value = node.getAttribute(attribute(key));
        if (value !== null) return value;
      }
      return Reflect.get(target, key, receiver);
    },
    set(target, key, value) {
      return typeof key === 'symbol' ? Reflect.set(target, key, value) : write(key, value);
    },
    deleteProperty(target, key) {
      if (typeof key === 'symbol') return Reflect.deleteProperty(target, key);
      node.removeAttribute(attribute(key));
      return true;
    },
    has(target, key) {
      return (typeof key === 'string' && node.hasAttribute(attribute(key))) || Reflect.has(target, key);
    },
    ownKeys(target) { return [...names(), ...Reflect.ownKeys(target)]; },
    getOwnPropertyDescriptor(target, key) {
      if (typeof key === 'string' && names().includes(key))
        return { value: node.getAttribute(attribute(key)), writable: true, enumerable: true, configurable: true };
      return Reflect.getOwnPropertyDescriptor(target, key);
    },
    defineProperty(target, key, descriptor) {
      if (typeof key === 'symbol') return Reflect.defineProperty(target, key, descriptor);
      if (!('value' in descriptor) || !descriptor.configurable ||
          !descriptor.enumerable || !descriptor.writable) return false;
      return write(key, descriptor.value);
    },
    preventExtensions() { return false; }
  });
  __datasets.set(node, map);
  return map;
}
