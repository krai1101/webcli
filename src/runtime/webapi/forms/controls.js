const __submittingForms = new WeakSet();
__installDomMembers(class {
  get checked() {
    return this.hasAttribute('checked');
  }
  set checked(value) {
    if (value && this.type === 'radio' && this.name)
      for (const n of this.getRootNode().querySelectorAll('input'))
        if (n !== this && n.type === 'radio' && n.name === this.name && n.form === this.form)
          n.removeAttribute('checked');
    value ? this.setAttribute('checked', '') : this.removeAttribute('checked');
  }
  get options() {
    return this.tagName === 'SELECT' ? this.querySelectorAll('option') : undefined;
  }
  get selectedIndex() {
    if (this.tagName !== 'SELECT') return undefined;
    return __webcliSelectedIndex(this.__id);
  }
  set selectedIndex(value) {
    if (this.tagName !== 'SELECT') return;
    __webcliSelectedIndex(this.__id, Number(value));
  }
  get selected() {
    for (let parent = this.parentNode; parent; parent = parent.parentNode)
      if (parent.tagName === 'SELECT') return parent.options[parent.selectedIndex] === this;
    return this.hasAttribute('selected');
  }
  set selected(value) {
    for (let parent = this.parentNode; parent; parent = parent.parentNode)
      if (parent.tagName === 'SELECT') {
        if (value)
          parent.selectedIndex = parent.options.indexOf(this);
        else if (this.selected)
          parent.selectedIndex = -1;
        return;
      }
    this.toggleAttribute('selected', !!value);
  }
  get multiple() {
    return this.hasAttribute('multiple');
  }
  set multiple(value) {
    this.toggleAttribute('multiple', !!value);
  }
  get disabled() {
    return this.hasAttribute('disabled');
  }
  set disabled(v) {
    v ? this.setAttribute('disabled', '') : this.removeAttribute('disabled');
  }
  get readOnly() {
    return this.hasAttribute('readonly');
  }
  set readOnly(value) {
    this.toggleAttribute('readonly', !!value);
  }
  get required() {
    return this.hasAttribute('required');
  }
  set required(v) {
    v ? this.setAttribute('required', '') : this.removeAttribute('required');
  }
  get hidden() {
    return this.hasAttribute('hidden');
  }
  set hidden(v) {
    v ? this.setAttribute('hidden', '') : this.removeAttribute('hidden');
  }
  get value() {
    if (this.tagName === 'TEXTAREA') return __webcliTextareaValue(this.__id);
    if (this.tagName === 'OPTION') return this.getAttribute('value') ?? this.textContent;
    if (this.tagName === 'SELECT') {
      return this.options[this.selectedIndex]?.value || '';
    }
    return this.getAttribute('value') ??
        (this.type === 'checkbox' || this.type === 'radio' ? 'on' : '');
  }
  set value(value) {
    value = this.tagName === 'TEXTAREA' && value === null ? '' : String(value);
    if (this.tagName === 'TEXTAREA')
      __webcliTextareaValue(this.__id, value);
    else if (this.tagName === 'SELECT')
      this.selectedIndex = this.options.findIndex(option => option.value === value);
    else
      this.setAttribute('value', value);
  }
  get form() {
    const id = this.getAttribute('form');
    if (id !== null) {
      const form = this.getRootNode().querySelectorAll('form[id]').find(node => node.id === id);
      return form || null;
    }
    for (let n = this.parentNode; n; n = n.parentNode)
      if (n.tagName === 'FORM') return n;
    return null;
  }
  get elements() {
    return __formElements(this);
  }
  get action() {
    const value = this.getAttribute('action');
    return value ? __webcliNavigate(value, document.baseURI) : location.href;
  }
  set action(v) {
    this.setAttribute('action', v);
  }
  get method() {
    const value = (this.getAttribute('method') || '').toLowerCase();
    return value === 'post' || value === 'dialog' ? value : 'get';
  }
  set method(v) {
    this.setAttribute('method', v);
  }
  checkValidity() {
    const fields = this.tagName === 'FORM' ? this.elements : [this];
    return fields.every(n => {
      if (__isControlDisabled(n) || __isReadOnlyControl(n) || !n.required ||
          ['hidden', 'button', 'submit', 'reset', 'image'].includes(n.type))
        return true;
      if (n.type === 'radio')
        return (n.form?.elements || fields)
            .some(other => other.type === 'radio' && other.name === n.name && other.checked);
      return n.type === 'checkbox' ? n.checked : !!n.value;
    });
  }
  submit() {
    if (this.tagName !== 'FORM') throw new TypeError('submit requires a form');
    __webcliSubmission = {form: this.__id, button: -1};
  }
  requestSubmit(button = null) {
    if (this.tagName !== 'FORM') throw new TypeError('requestSubmit requires a form');
    if (button && !['INPUT', 'BUTTON'].includes(button.tagName) ||
        button && button.type !== 'submit')
      throw new TypeError('Submit button required');
    if (button && button.form !== this)
      throw new DOMException('Submitter belongs to another form', 'NotFoundError');
    if (__submittingForms.has(this)) return;
    __submittingForms.add(this);
    try {
      if (!this.hasAttribute('novalidate') && !(button && button.hasAttribute('formnovalidate')) &&
          !this.checkValidity()) {
        for (const field of this.elements)
          if (!field.checkValidity()) field.dispatchEvent(new Event('invalid', {cancelable: true}));
        return;
      }
      const event = new SubmitEvent('submit', {
        bubbles: true, cancelable: true, submitter: button
      });
      if (__dispatchInputEvent(this, event))
        __webcliSubmission = {form: this.__id, button: button ? button.__id : -1};
    } finally {
      __submittingForms.delete(this);
    }
  }
});
