// Reflect editing state into the DOM, rather than an unrelated JS property.
Object.defineProperties(HTMLElement.prototype, {
  contentEditable: {
    configurable: true,
    get() {
      const value = this.getAttribute('contenteditable')?.toLowerCase();
      if (value === '' || value === 'true') return 'true';
      return value === 'false' || value === 'plaintext-only' ? value : 'inherit';
    },
    set(value) {
      value = String(value).toLowerCase();
      if (value === 'inherit') this.removeAttribute('contenteditable');
      else if (['true', 'false', 'plaintext-only'].includes(value))
        this.setAttribute('contenteditable', value);
      else throw new DOMException('Invalid contentEditable state', 'SyntaxError');
    }
  },
  isContentEditable: {
    configurable: true,
    get() {
      for (let node = this; node; node = node.parentElement) {
        const state = node.contentEditable;
        if (state === 'true' || state === 'plaintext-only') return true;
        if (state === 'false') return false;
      }
      return false;
    }
  }
});
