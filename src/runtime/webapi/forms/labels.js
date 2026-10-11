function __labelable(node) {
  return node instanceof HTMLElement &&
    (['BUTTON', 'METER', 'OUTPUT', 'PROGRESS', 'SELECT', 'TEXTAREA'].includes(node.tagName) ||
     node.tagName === 'INPUT' && node.type !== 'hidden');
}
Object.defineProperties(HTMLLabelElement.prototype, {
  htmlFor: {
    get() { return this.getAttribute('for') || ''; },
    set(value) { this.setAttribute('for', value); },
    configurable: true, enumerable: true
  },
  control: {
    get() {
      if (this.hasAttribute('for')) {
        const candidate = this.getRootNode().querySelectorAll('[id]').find(node => node.id === this.htmlFor);
        return __labelable(candidate) ? candidate : null;
      }
      return this.querySelectorAll('button,input,meter,output,progress,select,textarea')
        .find(__labelable) || null;
    },
    configurable: true, enumerable: true
  },
  form: {
    get() { return this.control?.form ?? null; },
    configurable: true, enumerable: true
  }
});
