__installDomMembers(class {
  get id() {
    return this.getAttribute('id') || '';
  }
  set id(v) {
    this.setAttribute('id', v);
  }
  get name() {
    return this.getAttribute('name') || '';
  }
  set name(v) {
    this.setAttribute('name', v);
  }
  get type() {
    const value = (this.getAttribute('type') || '').toLowerCase();
    if (this.tagName === 'BUTTON') return ['button', 'reset', 'submit'].includes(value) ? value :
      'submit';
    if (this.tagName === 'INPUT') return ['button', 'checkbox', 'color', 'date', 'datetime-local',
      'email', 'file', 'hidden', 'image', 'month', 'number', 'password', 'radio', 'range',
      'reset', 'search', 'submit', 'tel', 'text', 'time', 'url', 'week'
    ].includes(value) ? value : 'text';
    return value;
  }
  set type(v) {
    this.setAttribute('type', v);
  }
  get href() {
    const value = this.getAttribute('href');
    if (value === null) return '';
    try {
      return __parseURL(value, this.baseURI).href;
    } catch {
      return value;
    }
  }
  set href(v) {
    this.setAttribute('href', v);
  }
  get async() {
    return this.hasAttribute('async') || this._forceAsync !== false;
  }
  set async(value) {
    this._forceAsync = false;
    this.toggleAttribute('async', !!value);
  }
  get noModule() {
    return this.hasAttribute('nomodule');
  }
  get nonce() {
    return this.getAttribute('nonce') || '';
  }
  set nonce(value) {
    this.setAttribute('nonce', value);
  }
  set noModule(value) {
    this.toggleAttribute('nomodule', !!value);
  }
  get defer() {
    return this.hasAttribute('defer');
  }
  set defer(value) {
    this.toggleAttribute('defer', !!value);
  }
  get src() {
    const value = this.getAttribute('src');
    if (value === null) return '';
    try {
      return __parseURL(value, this.baseURI).href;
    } catch {
      return value;
    }
  }
  set src(v) {
    this.setAttribute('src', v);
  }
  get className() {
    return this.getAttribute('class') || '';
  }
  set className(v) {
    this.setAttribute('class', v);
  }
  get style() {
    if (!this._style) this._style = __inlineStyle(this);
    return this._style;
  }
  get classList() { return __classList(this); }
  set classList(value) { __classList(this).value = value; }
  get dataset() {
    return __dataset(this);
  }
});
