// Native form-control helpers used by documentation and ordinary HTML forms.
Object.defineProperty(HTMLTextAreaElement.prototype, 'defaultValue', {
  configurable: true,
  enumerable: true,
  get() { return this.textContent; },
  set(value) { this.textContent = String(value); }
});
HTMLSelectElement.prototype.add = function(element, before = null) {
  if (this.tagName !== 'SELECT' || !(element instanceof DomNode) || !['OPTION','OPTGROUP'].includes(element.tagName))
    throw new TypeError('select.add requires an option or optgroup');
  if (typeof before === 'number') before = this.options[before | 0] || null;
  if (before !== null && (!(before instanceof DomNode) || !this.contains(before)))
    throw new Error('reference option is not in this select');
  (before ? before.parentNode : this).insertBefore(element, before);
};
const __removeElement = DomNode.prototype.remove;
HTMLSelectElement.prototype.remove = function(index) {
  if (this.tagName === 'SELECT' && arguments.length) {
    const option = this.options[Number(index) | 0];
    if (option) __removeElement.call(option);
  } else __removeElement.call(this);
};
for (const constructor of [HTMLOptionElement,HTMLScriptElement]) {
  Object.defineProperty(constructor.prototype, 'text', {get() {return this.textContent;},
    set(value) {this.textContent = String(value);}});
}
for (const constructor of [HTMLOptionElement,HTMLOptGroupElement]) {
  Object.defineProperty(constructor.prototype, 'label', {get() {
    const label = this.getAttribute('label');
    return label !== null ? label : this.tagName === 'OPTION' ? this.textContent : '';
  }, set(value) {this.setAttribute('label',value);}});
}
