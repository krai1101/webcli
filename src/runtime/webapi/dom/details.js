// Attribute reflection and coalesced task events for disclosure widgets.
// No layout or automatic scrolling is involved.
(() => {
  const pending = new WeakMap();
  globalThis.ToggleEvent = class ToggleEvent extends Event {
    constructor(type, init = {}) {
      super(type, init);
      Object.defineProperties(this, {
        oldState: {value: String(init.oldState ?? ''), enumerable: true},
        newState: {value: String(init.newState ?? ''), enumerable: true},
        source: {value: init.source ?? null, enumerable: true}
      });
    }
  };
  globalThis.HTMLDetailsElement = class HTMLDetailsElement extends HTMLElement {
    static [Symbol.hasInstance](node) {
      return node instanceof DomNode && node.namespaceURI === __htmlNamespace &&
          node.tagName === 'DETAILS';
    }
    get open() {return this.hasAttribute('open');}
    set open(value) {this.toggleAttribute('open', Boolean(value));}
  };
  __elementTypes.set('DETAILS', HTMLDetailsElement);
  for (const node of __cachedNodes())
    if (node instanceof HTMLDetailsElement)
      Object.setPrototypeOf(node, HTMLDetailsElement.prototype);

  function changed(node, oldOpen) {
    if (oldOpen === node.open) return;
    let task = pending.get(node);
    if (task) {task.newState = node.open ? 'open' : 'closed'; return;}
    task = {oldState: oldOpen ? 'open' : 'closed', newState: node.open ? 'open' : 'closed'};
    pending.set(node, task);
    try {
      setTimeout(() => {
        pending.delete(node);
        node.dispatchEvent(new ToggleEvent('toggle', task));
      }, 0);
    } catch (error) {pending.delete(node); throw error;}
  }
  for (const name of ['setAttribute', 'removeAttribute', 'setAttributeNS', 'removeAttributeNS']) {
    const original = DomNode.prototype[name];
    DomNode.prototype[name] = function(...args) {
      const details = this instanceof HTMLDetailsElement, wasOpen = details && this.open;
      const result = original.apply(this, args);
      if (details) changed(this, wasOpen);
      return result;
    };
  }
})();
