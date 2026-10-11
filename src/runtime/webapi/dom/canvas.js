// Canvas elements participate in DOM feature detection without allocating a
// renderer. An unavailable drawing context is represented by null.
globalThis.HTMLCanvasElement = class HTMLCanvasElement extends HTMLElement {
  constructor() {
    throw new TypeError('Illegal HTMLCanvasElement constructor');
  }
  static [Symbol.hasInstance](node) {
    return node instanceof DomNode && node.namespaceURI === __htmlNamespace &&
      node.tagName === 'CANVAS';
  }
  getContext(contextId) {
    if (!(this instanceof HTMLCanvasElement))
      throw new TypeError('getContext requires an HTMLCanvasElement');
    if (!arguments.length) throw new TypeError('Context identifier required');
    if (typeof contextId === 'symbol')
      throw new TypeError('Cannot convert Symbol to string');
    // Preserve DOMString coercion, including exceptions from page objects.
    String(contextId);
    return null;
  }
};
