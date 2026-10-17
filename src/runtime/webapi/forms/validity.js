// Live constraint state shared by the DOM API and form submission validation.
(() => {
  const owners = new WeakMap(), cached = new WeakMap(), custom = new WeakMap(), token = {};
  const flags = ['valueMissing', 'typeMismatch', 'patternMismatch', 'tooLong', 'tooShort',
    'rangeUnderflow', 'rangeOverflow', 'stepMismatch', 'badInput', 'customError'];
  function values(node) {
    const result = Object.fromEntries(flags.map(flag => [flag, false]));
    result.customError = !!custom.get(node);
    if (!node.willValidate) return result;
    const value = String(node.value ?? ''), type = node.type;
    if (node.required) {
      if (type === 'radio') {
        const group = node.name ? (node.form?.elements || node.getRootNode().querySelectorAll('input'))
          .filter(other => other.type === 'radio' && other.name === node.name && other.form === node.form) : [node];
        result.valueMissing = !group.some(other => other.checked);
      } else result.valueMissing = type === 'checkbox' ? !node.checked : !value;
    }
    if (value && type === 'email') {
      const email = /^[a-zA-Z0-9.!#$%&'*+\/=?^_`{|}~-]+@[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?(?:\.[a-zA-Z0-9](?:[a-zA-Z0-9-]*[a-zA-Z0-9])?)*$/;
      result.typeMismatch = !(node.multiple ? value.split(',') : [value]).every(part => email.test(part.trim()));
    }
    if (value && type === 'url') {
      try { new URL(value); } catch (_) { result.typeMismatch = true; }
    }
    if (value && ['text', 'search', 'url', 'tel', 'email', 'password'].includes(type) && node.hasAttribute('pattern')) {
      try { result.patternMismatch = !new RegExp('^(?:' + node.getAttribute('pattern') + ')$', 'v').test(value); }
      catch (_) {} // Invalid patterns impose no constraint.
    }
    if (value && ['number', 'range'].includes(type) && Number.isFinite(Number(value))) {
      const bound = name => {
        const raw = node.getAttribute(name);
        return raw !== null && raw.trim() && Number.isFinite(Number(raw)) ? Number(raw) : null;
      };
      const minimum = bound('min'), maximum = bound('max');
      result.rangeUnderflow = minimum !== null && Number(value) < minimum;
      result.rangeOverflow = maximum !== null && Number(value) > maximum;
    }
    return result;
  }
  class ValidityState {
    constructor(key, owner) {
      if (key !== token) throw new TypeError('Illegal constructor');
      owners.set(this, owner);
    }
  }
  for (const flag of [...flags, 'valid'])
    Object.defineProperty(ValidityState.prototype, flag, {
      enumerable: true, configurable: true,
      get() {
        const owner = owners.get(this);
        if (!owner) throw new TypeError('ValidityState required');
        const current = values(owner);
        return flag === 'valid' ? !flags.some(name => current[name]) : current[flag];
      }
    });
  Object.defineProperty(ValidityState.prototype, Symbol.toStringTag, {value: 'ValidityState'});
  globalThis.ValidityState = ValidityState;
  for (const type of [HTMLInputElement, HTMLTextAreaElement, HTMLSelectElement, HTMLButtonElement])
    Object.defineProperties(type.prototype, {
      willValidate: {get() {
        return !__isControlDisabled(this) && !__isReadOnlyControl(this) &&
          !['hidden', 'button', 'reset'].includes(this.type) && !this.closest('datalist');
      }, configurable: true},
      validity: {get() {
        if (!cached.has(this)) cached.set(this, new ValidityState(token, this));
        return cached.get(this);
      }, configurable: true},
      validationMessage: {get() {
        return !this.willValidate || this.validity.valid ? '' : custom.get(this) || 'Please enter a valid value.';
      }, configurable: true},
      setCustomValidity: {value(message) { custom.set(this, String(message)); }, configurable: true},
      checkValidity: {value() {
        if (!this.willValidate || this.validity.valid) return true;
        __dispatchBrowserEvent.call(this, new Event('invalid', {cancelable: true}), true);
        return false;
      }, configurable: true},
      reportValidity: {value() { return this.checkValidity(); }, configurable: true}
    });
  Object.defineProperties(HTMLFormElement.prototype, {
    checkValidity: {value() {
      let valid = true;
      for (const field of this.elements)
        if (['INPUT', 'SELECT', 'TEXTAREA', 'BUTTON'].includes(field.tagName) && !field.checkValidity()) valid = false;
      return valid;
    }, configurable: true},
    reportValidity: {value() { return this.checkValidity(); }, configurable: true}
  });
})();
